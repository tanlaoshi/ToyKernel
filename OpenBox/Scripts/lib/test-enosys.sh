#!/usr/bin/expect -f
#
# test-enosys.sh — 开课前：未知 syscall → -ENOSYS（Guest ENOSYS.ELF）
# 在 ToyImage 根跑：./Scripts/test-enosys.sh（需 expect；ELF 由 ToyKernel ./build.sh 同步）
#
set LOG "/tmp/toyos-test-enosys.log"
set BOOT_TO 90
set PROMPT_TO 10
set CMD_TO 20
set HALT_TO 10

proc cleanup { } {
    catch { exec pkill -9 -f "qemu-system-x86_64" }
}

proc fail_dump { } {
    global LOG
    send_user "=== FAIL, log tail ===\n"
    catch { exec tail -80 $LOG } tail
    send_user "$tail\n"
    cleanup
    exit 1
}

cleanup
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

spawn sh -c "cd $image_root && exec $run_split --kill-qemu --headless --smp=1"

set timeout $BOOT_TO
expect {
    -re "ToyOS ready|ToyOS 就绪" {
        send_user "ok: boot\n"
    }
    timeout {
        send_user "timeout waiting ToyOS ready\n"
        fail_dump
    }
}

set timeout $PROMPT_TO
send "\r"
expect {
    -re "toyos>" {
        send_user "ok: prompt\n"
    }
    timeout {
        send_user "timeout waiting toyos>\n"
        fail_dump
    }
}

set timeout $CMD_TO
send "exec ENOSYS.ELF\r"
expect {
    -re "toyos>" {
        if {[regexp -- {enosys: ok} $expect_out(buffer)]} {
            send_user "ok: enosys demo\n"
        } else {
            send_user "assert fail: expected 'enosys: ok'\n"
            fail_dump
        }
    }
    timeout {
        send_user "timeout after exec ENOSYS.ELF\n"
        fail_dump
    }
}

send "halt\r"
set timeout $HALT_TO
expect {
    eof { send_user "ok: qemu exited\n" }
    timeout {
        send_user "halt: killing qemu\n"
        cleanup
    }
}

send_user "=== PASS ===\n"
exit 0
