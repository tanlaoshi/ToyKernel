#!/usr/bin/expect -f
#
# test-shell.sh — PR-TEST-1：自动化功能测试第 1 步（Shell 命令）。
# 规格：Documents/待做/自动化功能测试.md。
#
# 用 expect 拉起 run-split.sh --kill-qemu --headless，等 ToyOS ready 后逐条
# 喂 Shell 命令并检查串口输出。断言只看「该命令之后、下一次 toyos> 之前」
# 的新输出（用 expect_out(buffer)）。失败打印日志尾部并 exit 1。
#
# 运行：./Scripts/test-shell.sh  （需先 sudo apt install expect）

set LOG "/tmp/toyos-test-shell.log"

# ---- 超时（秒）----
set BOOT_TO   90
set PROMPT_TO 10
set CMD_TO    15
set HALT_TO   10

# ---- 清理：杀掉 QEMU ----
proc cleanup { } {
    catch { exec pkill -9 -f "qemu-system-x86_64" }
}

# ---- 失败：打印日志尾部并退出 ----
proc fail_dump { } {
    global LOG
    send_user "=== FAIL, log tail ===\n"
    catch { exec tail -80 $LOG } tail
    send_user "$tail\n"
    cleanup
    exit 1
}

# ---- 跑一条命令并断言输出含 Need ----
proc run_cmd {Cmd Need} {
    global PROMPT_TO CMD_TO
    send "$Cmd\r"
    expect {
        -re "toyos>" {
            if {[regexp -- $Need $expect_out(buffer)]} {
                send_user "ok: $Cmd -> $Need\n"
            } else {
                send_user "assert fail: '$Cmd' expected '$Need'\n"
                fail_dump
            }
        }
        timeout {
            send_user "timeout waiting prompt after: $Cmd\n"
            fail_dump
        }
    }
}

# ---- 开跑前先清残留 QEMU ----
cleanup

# ---- 日志 ----
log_file -noappend $LOG

proc find_image_root {} {
    set cand [pwd]
    if {[file exists [file join $cand RootFs X64 TOYOS.ID]]} { return $cand }
    if {[file exists [file join $cand TOYOS.ID]] && [file exists [file join $cand Kernel.elf]]} {
        return [file dirname $cand]
    }
    foreach up {. .. ../.. ../../..} {
        set t [file normalize [file join [pwd] $up ToyImage]]
        if {[file exists [file join $t RootFs X64 TOYOS.ID]]} { return $t }
        set t2 [file normalize [file join [pwd] $up]]
        if {[file exists [file join $t2 RootFs X64 TOYOS.ID]]} { return $t2 }
    }
    return ""
}
set image_root [find_image_root]
if {$image_root eq ""} {
    send_user "error: ToyImage not found (need RootFs/X64/TOYOS.ID)\n"
    exit 2
}
set run_split [file normalize [file join $image_root .. Scripts lib run-split.sh]]
if {![file exists $run_split] && [info exists env(TOYOS_ROOT)]} {
    set run_split [file join $env(TOYOS_ROOT) Scripts lib run-split.sh]
}
if {![file exists $run_split]} {
    send_user "error: run-split.sh not found under Scripts/lib\n"
    exit 2
}

spawn sh -c "cd $image_root && exec $run_split --kill-qemu --headless"

# ---- 等启动 ----
set timeout $BOOT_TO
expect {
    -re "ToyOS ready|ToyOS 就绪" {
        send_user "ok: boot -> ToyOS ready\n"
    }
    timeout {
        send_user "timeout waiting ToyOS ready\n"
        fail_dump
    }
}

# ---- 开壳：第一个回车 ----
set timeout $PROMPT_TO
send "\r"
expect {
    -re "toyos>" {
        send_user "ok: shell prompt\n"
    }
    timeout {
        send_user "timeout waiting first toyos>\n"
        fail_dump
    }
}

# ---- 五条命令 ----
set timeout $CMD_TO
run_cmd "help"         "commands:"
run_cmd "ls"           "TOYOS.DB"
run_cmd "lsdev"        "Bound Drivers"
run_cmd "show memory"  "physical memory"
run_cmd "list tasks"   "pid="

# ---- 结束：halt 等 QEMU 退出；未退出则 kill，仍算 PASS（规格第 4-5 步）----
send "halt\r"
set timeout $HALT_TO
expect {
    eof {
        send_user "ok: qemu exited\n"
    }
    timeout {
        send_user "halt: qemu did not exit (CPU parked), killing\n"
        cleanup
    }
}

send_user "=== PASS ===\n"
exit 0
