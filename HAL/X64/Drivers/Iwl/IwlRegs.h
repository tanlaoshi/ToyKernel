/*
 * IwlRegs.h — 8265/family-8000 CSR·FH·PRPH（对照 FreeBSD if_iwmreg.h，只读）
 */
#ifndef IWL_REGS_H
#define IWL_REGS_H

#define IWL_CSR_HW_IF_CONFIG          0x000u
#define IWL_CSR_INT                   0x008u
#define IWL_CSR_INT_MASK              0x00cu
#define IWL_CSR_FH_INT_STATUS         0x010u
#define IWL_CSR_RESET                 0x020u
#define IWL_CSR_GP_CNTRL              0x024u
#define IWL_CSR_HW_REV                0x028u
#define IWL_CSR_GIO_REG               0x03cu
#define IWL_CSR_UCODE_DRV_GP1_CLR     0x05cu
#define IWL_CSR_MBOX_SET              0x088u
#define IWL_CSR_GIO_CHICKEN           0x100u
#define IWL_CSR_DBG_HPET              0x240u
#define IWL_CSR_DBG_LINK_PWR          0x250u
#define IWL_CSR_MAC_SHADOW_CTRL       0x0a8u
#define IWL_CSR_CTXT_INFO_BA          0x040u /* unused on FH-load path */

#define IWL_CSR_HW_IF_HAP_WAKE_L1A    0x00080000u
#define IWL_CSR_HW_IF_NIC_READY       0x00400000u /* PCI_OWN_SEM / OWN_SET */
#define IWL_CSR_HW_IF_PREPARE         0x08000000u /* WAKE_ME */
#define IWL_CSR_MBOX_OS_ALIVE         0x20u
#define IWL_CSR_GIO_L0S_ENABLED       0x00000002u

#define IWL_CSR_RESET_SW              0x00000080u
#define IWL_CSR_RESET_MASTER_DIS      0x00000100u
#define IWL_CSR_RESET_STOP_MASTER     0x00000200u
#define IWL_CSR_LINK_PWR_MGMT_DIS     0x80000000u /* on DBG_LINK_PWR */

#define IWL_CSR_GP_MAC_CLOCK_READY    0x00000001u
#define IWL_CSR_GP_INIT_DONE          0x00000004u
#define IWL_CSR_GP_MAC_ACCESS_REQ     0x00000008u
#define IWL_CSR_GP_GOING_TO_SLEEP     0x00000010u
#define IWL_CSR_GP_MAC_ACCESS_EN      0x00000001u
#define IWL_CSR_GP_RFKILL_SW          0x08000000u

#define IWL_CSR_INT_ALIVE             (1u << 0)
#define IWL_CSR_INT_FH_TX             (1u << 27)
#define IWL_CSR_INT_FH_RX             (1u << 31)

#define IWL_CSR_GIO_L1A_NO_L0S_RX     0x00800000u
#define IWL_CSR_DBG_HPET_VAL          0xFFFF0000u
#define IWL_CSR_UCODE_SW_RFKILL       0x00000002u
#define IWL_CSR_UCODE_CMD_BLOCKED     0x00000004u

#define IWL_HBUS_TARG_PRPH_WADDR      0x444u
#define IWL_HBUS_TARG_PRPH_RADDR      0x448u
#define IWL_HBUS_TARG_PRPH_WDAT       0x44cu
#define IWL_HBUS_TARG_PRPH_RDAT       0x450u
#define IWL_HBUS_TARG_WRPTR           0x460u
#define IWL_HBUS_TARG_MEM_WADDR       0x410u
#define IWL_HBUS_TARG_MEM_WDAT        0x418u

#define IWL_CSR_FH_INT_TX_MASK        0x00000003u /* TX_CHNL0|1 */
#define IWL_FH_KW_MEM_ADDR            0x197Cu
#define IWL_FH_UCODE_LOAD_STATUS      0x1af0u
#define IWL_FH_SRVC_CHNL              9
#define IWL_FH_SRVC_SRAM_ADDR         0x19C8u
#define IWL_FH_TFDIB_CTRL0_9          0x1948u
#define IWL_FH_TFDIB_CTRL1_9          0x194Cu
#define IWL_FH_TCSR_CONFIG_9          0x1E20u
#define IWL_FH_TCSR_BUF_STS_9         0x1E28u
#define IWL_FH_TB_MAX                 0x20000u
#define IWL_FH_TFDIB_HI_SHIFT         28
#define IWL_FH_TCSR_PAUSE             0x00000000u
#define IWL_FH_TCSR_ENABLE            0x80000000u
#define IWL_FH_TCSR_CIRQ_ENDTFD       0x00100000u
#define IWL_FH_TCSR_CREDIT_DISABLE    0x00000000u
#define IWL_FH_TCSR_TFDB_VALID        0x3u
#define IWL_FH_TCSR_TB_NUM_POS        20
#define IWL_FH_TCSR_TB_IDX_POS        12
#define IWL_FH_TX_CHICKEN             0x1E98u
#define IWL_FH_TX_CHICKEN_RETRY       0x2u
#define IWL_FH_TSSR_TX_STATUS         0x1EB0u
#define IWL_FH_TSSR_TX_ERROR          0x1EB8u
#define IWL_FH_TCSR_CHNL_NUM          8
#define IWL_FH_TCSR_CHNL_CFG(n)       (0x1D00u + 0x20u * (UINT32)(n))
#define IWL_FH_TCSR_DMA_CREDIT_EN     0x00000008u
#define IWL_FH_TCSR_DMA_CHNL_EN       0x80000000u

#define IWL_FH_RSCSR_STTS_WPTR        0x1BC0u
#define IWL_FH_RSCSR_RBDCB_BASE       0x1BC4u
#define IWL_FH_RSCSR_RBDCB_WPTR       0x1BC8u
#define IWL_FH_RCSR_CHNL0_CONFIG      0x1C00u
#define IWL_FH_RCSR_EN                0x80000000u
#define IWL_FH_RCSR_IRQ_HOST          0x00001000u
#define IWL_FH_RCSR_RB_SIZE_4K        0x00000000u
#define IWL_FH_RCSR_IGNORE_EMPTY      0x00000004u /* HW bug W/A */
#define IWL_FH_RCSR_RBDCB_SIZE_POS    20
#define IWL_FH_CBBC_QUEUE(n)          (0x19D0u + 4u * (UINT32)(n))

#define IWL_RELEASE_CPU_RESET         0x300cu
#define IWL_RELEASE_CPU_RESET_BIT     0x01000000u
#define IWL_LMPM_CHICK                0xa01ff8u
#define IWL_LMPM_CHICK_EXT_ADDR       0x01u
#define IWL_FW_MEM_EXT_START          0x40000u
#define IWL_FW_MEM_EXT_END            0x57FFFu
#define IWL_SB_CPU_1_STATUS           0xa01e30u
#define IWL_SB_CPU_2_STATUS           0xa01e34u
#define IWL_SCD_BASE                  0xa02c00u
#define IWL_SCD_SRAM_BASE_ADDR        (IWL_SCD_BASE + 0x0u)
#define IWL_SCD_DRAM_BASE_ADDR        (IWL_SCD_BASE + 0x8u)
#define IWL_SCD_TXFACT                (IWL_SCD_BASE + 0x10u)
#define IWL_SCD_QUEUECHAIN_SEL        (IWL_SCD_BASE + 0xe8u)
#define IWL_SCD_CHAINEXT_EN           (IWL_SCD_BASE + 0x244u)
#define IWL_SCD_AGGR_SEL              (IWL_SCD_BASE + 0x248u)
#define IWL_SCD_EN_CTRL               (IWL_SCD_BASE + 0x254u)
#define IWL_SCD_GP_CTRL               (IWL_SCD_BASE + 0x1a8u)
#define IWL_SCD_GP_ENABLE_31_QUEUES   0x00000001u /* BIT(0) */
#define IWL_SCD_GP_AUTO_ACTIVE        0x00040000u /* BIT(18) */
#define IWL_SCD_CONTEXT_MEM_LO        0x600u
#define IWL_SCD_TRANS_TBL_MEM_HI      0x808u
#define IWL_SCD_CONTEXT_QUEUE_OFF(q)  (IWL_SCD_CONTEXT_MEM_LO + ((UINT32)(q) * 8u))
#define IWL_SCD_QUEUE_WRPTR(q)        (IWL_SCD_BASE + 0x18u + (UINT32)(q) * 4u)
#define IWL_SCD_QUEUE_RDPTR(q)        (IWL_SCD_BASE + 0x68u + (UINT32)(q) * 4u)
#define IWL_SCD_QUEUE_STATUS_BITS(q)  (IWL_SCD_BASE + 0x10cu + (UINT32)(q) * 4u)
#define IWL_SCD_STTS_TXF_POS          0
#define IWL_SCD_STTS_ACTIVE_POS       3
#define IWL_SCD_STTS_WSL_POS          4
#define IWL_SCD_STTS_ACT_EN_POS       19
#define IWL_SCD_STTS_MSK              0x017F0000u
#define IWL_SCD_CTX_WIN_POS           0
#define IWL_SCD_CTX_FRAME_POS         16
#define IWL_TX_FIFO_CMD               7u
#define IWL_TX_FIFO_VO                3u /* AC_VO：mgmt/EAPOL */
#define IWL_TX_FIFO_BE                1u
#define IWL_TX_CRC_SIZE               4u
#define IWL_TX_DELIMITER_SIZE         4u
#define IWL_TFD_BC_SIZE               320u /* 256+64 */
#define IWL_TFD_NUM_TBS               20
#define IWL_TFD_BYTES                 128

#define IWL_TLV_MAGIC                 0x0a4c5749u
#define IWL_TLV_SEC_RT                19u
#define IWL_TLV_SEC_INIT              20u
#define IWL_TLV_DEF_CALIB             22u
#define IWL_TLV_PHY_SKU               23u
#define IWL_TLV_PAGING                32u
#define IWL_CPU1_CPU2_SEP             0xFFFFCCCCu
#define IWL_PAGING_SEP                0xAAAABBBBu

#define IWL_ALIVE                     0x01u
#define IWL_ALIVE_OK                  0xCAFEu
#define IWL_INIT_COMPLETE_NOTIF       0x04u
#define IWL_RX_FRAME_SIZE_MSK         0x00003fffu
#define IWL_RX_FRAME_INVALID          0x55550000u
#define IWL_RX_FRAME_ALIGN            0x40u

#define IWL_LEGACY_GROUP              0x0u
#define IWL_LONG_GROUP                0x1u
#define IWL_CMD_ID(op, grp, ver) \
    ((UINT32)(op) | ((UINT32)(grp) << 8) | ((UINT32)(ver) << 16))

#define IWL_CMD_PHY_CONTEXT           0x08u
#define IWL_FW_CTXT_ACTION_ADD        1u
#define IWL_FW_CTXT_ACTION_MODIFY     2u
#define IWL_PHY_BAND_24               1u
#define IWL_PHY_VHT_CHANNEL_MODE20    0u
#define IWL_PHY_VHT_CTRL_POS_1_BELOW  0u
#define IWL_PHY_RX_CHAIN_VALID_POS    1u
#define IWL_PHY_RX_CHAIN_CNT_POS      10u
#define IWL_PHY_RX_CHAIN_MIMO_CNT_POS 12u
#define IWL_CMD_SCAN_CFG              0x0cu
#define IWL_CMD_SCAN_REQ_UMAC         0x0du
#define IWL_CMD_ADD_STA_KEY           0x17u
#define IWL_CMD_ADD_STA               0x18u
/* OpenBSD iwm_add_sta_key_cmd_v1 / Linux STA_KEY_FLG_*（刀 #180） */
#define IWL_STA_KEY_FLG_CCM           0x0002u
#define IWL_STA_KEY_FLG_WEP_KEY_MAP   0x0008u
#define IWL_STA_KEY_FLG_KEYID_POS     8u
#define IWL_STA_KEY_FLG_KEYID_MSK     0x0300u
#define IWL_STA_KEY_MULTICAST         0x4000u
#define IWL_ADD_STA_KEY_CMD_V1_SIZE   64u
/* RX_MPDU_RES_STATUS（刀 #179/#180） */
#define IWL_RX_MPDU_MIC_OK            (1u << 6)
#define IWL_RX_MPDU_DEC_DONE          (1u << 11)
#define IWL_CMD_SCD_QUEUE_CFG         0x1du
#define IWL_CMD_TX                    0x1cu
#define IWL_CMD_LQ                    0x4eu /* 数据帧前必须给 AP 站一张速率表 */
#define IWL_CMD_MAC_CONTEXT           0x28u
#define IWL_CMD_BINDING               0x2bu
#define IWL_FW_MAC_TYPE_LISTENER      2u
#define IWL_FW_MAC_TYPE_BSS_STA       5u
#define IWL_MAC_FILTER_IN_PROBE_REQUEST     (1u << 12)
#define IWL_TSF_ID_A                  0u
#define IWL_MAC_FILTER_IN_PROMISC           (1u << 0)
#define IWL_MAC_FILTER_IN_CONTROL_AND_MGMT  (1u << 1)
#define IWL_MAC_FILTER_ACCEPT_GRP           (1u << 2)
#define IWL_MAC_FILTER_DIS_DECRYPT          (1u << 3)
#define IWL_MAC_FILTER_DIS_GRP_DECRYPT      (1u << 4)
#define IWL_MAC_FILTER_IN_BEACON            (1u << 6)
#define IWL_MAC_FLG_SHORT_PREAMBLE          0x20u
#define IWL_MAC_FLG_SHORT_SLOT              0x10u
#define IWL_MAX_MACS_IN_BINDING       3u
#define IWL_FW_CTXT_INVALID           0xffffffffu
#define IWL_LMAC_24G_INDEX            0u
#define IWL_MAC_CTX_CMD_SIZE          148u /* common 100 + union max(p2p_sta)=48 */
#define IWL_BINDING_CMD_V1_SIZE       24u  /* id+action+macs[3]+phy */
#define IWL_FW_PAGING_BLOCK_CMD       0x4fu
#define IWL_CMD_PHY_CONFIG            0x6au
#define IWL_CALIB_RES_NOTIF_PHY_DB    0x6bu
#define IWL_CMD_PHY_DB                0x6cu
#define IWL_CMD_TX_ANT_CFG            0x98u
#define IWL_CMD_NVM_ACCESS            0x88u
#define IWL_CMD_MCC_UPDATE           0xc8u
/* OpenBSD/Linux UMAC：0x0f；旧 LMAC 完成通知曾用 0xf8 */
#define IWL_SCAN_COMPLETE_UMAC        0x0fu
#define IWL_SCAN_COMPLETE_LMAC        0xf8u
#define IWL_BEACON_NOTIFICATION       0x90u
#define IWL_RX_MPDU_CMD               0xc1u

#define IWL_FW_CTXT_ID_POS            0u
#define IWL_FW_CTXT_COLOR_POS         8u
#define IWL_FW_CMD_ID_AND_COLOR(id, color) \
    (((UINT32)(id) << IWL_FW_CTXT_ID_POS) \
     | ((UINT32)(color) << IWL_FW_CTXT_COLOR_POS))
#define IWL_STA_LINK                  0u
#define IWL_STA_AUX_ACTIVITY          4u
#define IWL_STA_FLG_CLASS_AUTH        (1u << 14)
#define IWL_STA_FLG_CLASS_ASSOC       (1u << 15)
#define IWL_STA_MODE_MODIFY           1u
#define IWL_STA_MODIFY_TID_DISABLE_TX (1u << 1)
#define IWL_STA_MODIFY_QUEUES         (1u << 7)
#define IWL_DQA_MIN_MGMT_QUEUE        5u /* OpenBSD LINK：q5..8；q2=P2P */
#define IWL_DQA_MAX_MGMT_QUEUE        8u
#define IWL_DQA_BSS_CLIENT_QUEUE      4u /* Linux：BSS 关联后保证有队列 */
#define IWL_MAX_TID_COUNT             8u
#define IWL_ADD_STA_CMD_SIZE          48u /* ADD_STA_CMD_API_S_VER_10 */
#define IWL_ADD_STA_CMD_V7_SIZE       44u /* OpenBSD 8265 实发 */
#define IWL_SCD_TXQ_CFG_CMD_SIZE      12u
#define IWL_CMD_TIME_EVENT            0x29u
#define IWL_TE_BSS_STA_AGGRESSIVE_ASSOC 0u
#define IWL_TE_V2_NOTIF_HOST_EVENT_START (1u << 0)
#define IWL_TE_V2_NOTIF_HOST_EVENT_END   (1u << 1)
#define IWL_TE_V2_START_IMMEDIATELY      (1u << 11)
#define IWL_TE_V2_FRAG_NONE           0xffu
#define IWL_TIME_EVENT_CMD_SIZE       36u
#define IWL_MCC_SOURCE_OLD_FW         0u
#define IWL_MCC_SOURCE_GET_CURRENT    0x10u
#define IWL_MCC_UPDATE_V1_SIZE        4u
#define IWL_MCC_UPDATE_SIZE           28u /* VER_2：key + reserved2[5] */

#define IWL_PHY_DB_CFG                1u
#define IWL_PHY_DB_CALIB_NCH          2u
#define IWL_PHY_DB_CALIB_CHG_PAPD     4u
#define IWL_PHY_DB_CALIB_CHG_TXP      5u
#define IWL_UCODE_TYPE_REGULAR        0u
#define IWL_UCODE_TYPE_INIT           1u
#define IWL_UCODE_TYPE_MAX            4u

/* FW paging（CPU2）：CSS 4K + N×32K；addr 报给固件时 >>12 */
#define IWL_PAGE_2_EXP_SIZE           12u
#define IWL_FW_PAGING_SIZE            (1u << IWL_PAGE_2_EXP_SIZE)
#define IWL_PAGE_PER_GROUP_2_EXP      3u
#define IWL_NUM_PAGE_PER_GROUP        (1u << IWL_PAGE_PER_GROUP_2_EXP)
#define IWL_PAGING_BLOCK_SIZE         (IWL_NUM_PAGE_PER_GROUP * IWL_FW_PAGING_SIZE)
#define IWL_BLOCK_2_EXP_SIZE          (IWL_PAGE_2_EXP_SIZE + IWL_PAGE_PER_GROUP_2_EXP)
#define IWL_NUM_FW_PAGING_BLOCKS      33u
#define IWL_PAGING_CMD_IS_SECURED     (1u << 9)
#define IWL_PAGING_CMD_IS_ENABLED     (1u << 8)
#define IWL_PAGING_CMD_LAST_PAGES_POS 0u
#define IWL_MAX_PAGING_IMAGE_SIZE     (IWL_PAGING_BLOCK_SIZE * (IWL_NUM_FW_PAGING_BLOCKS - 1u))

#define IWL_AUX_STA_ID                1u
#define IWL_AP_STA_ID                 0u /* 关联后 BSS/AP 站 */
#define IWL_AUX_QUEUE                 15u
#define IWL_MAC_INDEX_AUX             4u
#define IWL_TX_FIFO_MCAST             5u
#define IWL_FRAME_LIMIT               64u
#define IWL_ANT_A                     (1u << 0)
#define IWL_ANT_B                     (1u << 1)
#define IWL_ANT_AB                    (IWL_ANT_A | IWL_ANT_B)
#define IWL_SCAN_NCHAN_CAPA           52u /* TLV N_SCAN_CHANNELS；旧 40 错 */
#define IWL_PROBE_OPTION_MAX          20u
#define IWL_SCAN_PROBE_REQ_SIZE       512u
#define IWL_SSID_IE_SIZE              34u /* id+len+32 */
#define IWL_CMD_PAYLOAD_MAX           4000u

#define IWL_SCAN_CFG_FLAG_ACTIVATE              (1u << 0)
#define IWL_SCAN_CFG_FLAG_ALLOW_CHUB            (1u << 3)
#define IWL_SCAN_CFG_FLAG_SET_TX_CHAINS         (1u << 8)
#define IWL_SCAN_CFG_FLAG_SET_RX_CHAINS         (1u << 9)
#define IWL_SCAN_CFG_FLAG_SET_AUX_STA_ID        (1u << 10)
#define IWL_SCAN_CFG_FLAG_SET_ALL_TIMES         (1u << 11)
#define IWL_SCAN_CFG_FLAG_SET_CHANNEL_FLAGS     (1u << 13)
#define IWL_SCAN_CFG_FLAG_SET_LEGACY_RATES      (1u << 14)
#define IWL_SCAN_CFG_FLAG_SET_MAC_ADDR          (1u << 15)
#define IWL_SCAN_CFG_FLAG_CLEAR_FRAGMENTED      (1u << 17)
#define IWL_SCAN_CFG_N_CHANNELS(n)              ((UINT32)(n) << 26)

#define IWL_SCAN_CFG_RATE_6M   (1u << 0)
#define IWL_SCAN_CFG_RATE_9M   (1u << 1)
#define IWL_SCAN_CFG_RATE_12M  (1u << 2)
#define IWL_SCAN_CFG_RATE_18M  (1u << 3)
#define IWL_SCAN_CFG_RATE_24M  (1u << 4)
#define IWL_SCAN_CFG_RATE_36M  (1u << 5)
#define IWL_SCAN_CFG_RATE_48M  (1u << 6)
#define IWL_SCAN_CFG_RATE_54M  (1u << 7)
#define IWL_SCAN_CFG_RATE_1M   (1u << 8)
#define IWL_SCAN_CFG_RATE_2M   (1u << 9)
#define IWL_SCAN_CFG_RATE_5M   (1u << 10)
#define IWL_SCAN_CFG_RATE_11M  (1u << 11)
#define IWL_SCAN_CFG_SUPPORTED_RATE(r) ((r) << 16)

#define IWL_UMAC_SCAN_GEN_PASS_ALL       (1u << 2)
#define IWL_UMAC_SCAN_GEN_PASSIVE        (1u << 3)
#define IWL_UMAC_SCAN_GEN_PRE_CONNECT    (1u << 4)
#define IWL_UMAC_SCAN_GEN_ITER_COMPLETE  (1u << 5)
#define IWL_UMAC_SCAN_GEN_EXTENDED_DWELL (1u << 10)
#define IWL_UMAC_SCAN_GEN_ADAPTIVE_DWELL (1u << 13)

#define IWL_SCAN_PRIORITY_HIGH           2u
#define IWL_SCAN_PRIORITY_EXT_6          6u
#define IWL_SCAN_START_UMAC              0xb2u
#define IWL_SCAN_ITERATION_COMPLETE_UMAC 0xb5u

#define IWL_RX_Q_SIZE                 256
#define IWL_RX_Q_MASK                 (IWL_RX_Q_SIZE - 1)
#define IWL_RX_Q_SIZE_LOG             8
#define IWL_CMD_Q_SIZE                32
#define IWL_CMD_Q_MASK                (IWL_CMD_Q_SIZE - 1)
/* HW TFD 环固定 256（Linux max_tfd_queue_size）；cmd 缓冲窗口仍 32 */
#define IWL_TFD_Q_SIZE                256
#define IWL_TFD_Q_MASK                (IWL_TFD_Q_SIZE - 1)
/* 8265 + 现代固件：DQA 命令队列为 0（非 legacy 9）；UMAC 只听 q0 */
#define IWL_CMD_QUEUE                 0
#define IWL_DQA_AUX_QUEUE             1
#define IWL_DATA_PATH_GROUP           0x5u
#define IWL_DQA_ENABLE_CMD            0x00u
#define IWL_TX_CMD_HDR_SIZE           56u /* 至 reserved4；后接 802.11 */
#define IWL_TX_CMD_FLG_ACK            (1u << 3)
#define IWL_TX_CMD_FLG_BT_DIS         (1u << 12)
#define IWL_TX_CMD_FLG_SEQ_CTL        (1u << 13)
#define IWL_TX_CMD_FLG_MH_PAD         (1u << 20) /* 26/30 字节头后有 2 字节填充 */
#define IWL_TX_CMD_FLG_RESP_TO_DRV    (1u << 21) /* 清掉则 TX 回执只留在固件里 */
#define IWL_TX_CMD_SEC_CCM            0x02u /* 固件用 TX 命令里的密钥做 CCMP */
#define IWL_TX_CMD_LIFE_INFINITE      0xffffffffu
#define IWL_TID_NON_QOS               0u
#define IWL_RATE_1M_PLCP              10u
#define IWL_RATE_6M_PLCP              13u /* OFDM 6Mbps；CCK 1M 常被 HT AP 丢掉数据帧 */
#define IWL_RATE_MCS_CCK              (1u << 9)
#define IWL_RATE_MCS_ANT_A            (1u << 14)
#define IWL_PM_FRAME_MGMT             2u
#define IWL_PM_FRAME_ASSOC            3u
#define IWL_FIRST_TB_SIZE             20u
#define IWL_FIRST_TB_ALIGN            64u
#define IWL_BAR_MAP_BYTES             0x10000u
#define IWL_FW_SEC_MAX                16
#define IWL_SSID_MAX                  32
#define IWL_PSK_MAX                   63

#endif
