/** @file

  Copyright (c) 2019, Intel Corporation. All rights reserved.<BR>
  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php.

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/
#include <PiPei.h>
#include <Library/BaseLib.h>
#include <Library/BoardInitLib.h>
#include <Library/BootloaderCoreLib.h>
#include <Library/SerialPortLib.h>
#include <Library/SiGpioLib.h>
#include <Library/PlatformHookLib.h>
#include <Library/FirmwareUpdateLib.h>
#include <Library/DebugLib.h>
#include <GpioPinsCnlLp.h>
#include <RegAccess.h>
#include <FsptUpd.h>
#include <PlatformData.h>

#define UCODE_REGION_BASE   FixedPcdGet32(PcdUcodeBase)
#define UCODE_REGION_SIZE   FixedPcdGet32(PcdUcodeSize)
// PcdPayloadBase is too big. using following value
// self.TOP_SWAP_SIZE + self.SLIMBOOTLOADER_SIZE + self.STAGE1B_SIZE
#define CODE_REGION_BASE    0xFFCE0000
#define CODE_REGION_SIZE    ((UINT32)~CODE_REGION_BASE + 1)

CONST
FSPT_UPD TempRamInitParams = {
  .FspUpdHeader = {
    .Signature = FSPT_UPD_SIGNATURE,
    .Revision  = 1,
    .Reserved  = {0},
  },
  .FsptCoreUpd = {
    .MicrocodeRegionBase   = UCODE_REGION_BASE,
    .MicrocodeRegionSize   = UCODE_REGION_SIZE,
    .CodeRegionBase        = CODE_REGION_BASE,
    .CodeRegionSize        = CODE_REGION_SIZE,
    .Reserved              = {0},
  },
  .FsptConfig = {
    .PcdSerialIoUartDebugEnable = 0,
    .PcdSerialIoUartNumber      = 0,
    .PcdSerialIoUart0PinMuxing  = 0,
    .UnusedUpdSpace0            = 0,
    .PcdSerialIoUartInputClock  = 0,
    .PcdPciExpressBaseAddress   = FixedPcdGet32 (PcdPciMmcfgBase),
    .PcdPciExpressRegionLength  = 0x20000000,
    .ReservedFsptUpd1           = {0},
  },
  .UpdTerminator = 0x55AA,
};

static GPIO_INIT_CONFIG mUartGpioTable[] = {
  {GPIO_CNL_LP_GPP_C8,  {GpioPadModeNative1, GpioHostOwnGpio, GpioDirNone,  GpioOutDefault, GpioIntDis, GpioHostDeepReset,  GpioTermNone}},//SERIALIO_UART0_RXD
  {GPIO_CNL_LP_GPP_C9,  {GpioPadModeNative1, GpioHostOwnGpio, GpioDirNone,  GpioOutDefault, GpioIntDis, GpioHostDeepReset,  GpioTermNone}},//SERIALIO_UART0_TXD
  {GPIO_CNL_LP_GPP_C12, {GpioPadModeNative1, GpioHostOwnGpio, GpioDirNone,  GpioOutDefault, GpioIntDis, GpioHostDeepReset,  GpioTermNone}},//SERIALIO_UART1_RXD
  {GPIO_CNL_LP_GPP_C13, {GpioPadModeNative1, GpioHostOwnGpio, GpioDirNone,  GpioOutDefault, GpioIntDis, GpioHostDeepReset,  GpioTermNone}},//SERIALIO_UART1_TXD
  {GPIO_CNL_LP_GPP_C20, {GpioPadModeNative1, GpioHostOwnGpio, GpioDirNone,  GpioOutDefault, GpioIntDis, GpioHostDeepReset,  GpioTermNone}},//SERIALIO_UART2_RXD
  {GPIO_CNL_LP_GPP_C21, {GpioPadModeNative1, GpioHostOwnGpio, GpioDirNone,  GpioOutDefault, GpioIntDis, GpioHostDeepReset,  GpioTermNone}},//SERIALIO_UART2_TXD
};

typedef enum {
  BootPartition1,
  BootPartition2,
  BootPartitionMax
} BOOT_PARTITION_SELECT;

/**
  Determines the boot partition that the platform firmware is booting from

  @param[out] BootPartition   The Boot partition the platform is booting from

  @retval     EFI_SUCCESS     The operation completed successfully.
**/
EFI_STATUS
EFIAPI
GetBootPartition (
  OUT BOOT_PARTITION_SELECT      *BootPartition
  )
{
  UINT32    P2sbBase;
  UINT32    P2sbBar;
  //UINT16    RegOffset;
  //UINT8     RtcPortId;
  UINT32    Data32;
  BOOLEAN   P2sbIsHidden;

  //
  //
  // get Top swap register Bit0 in PCH Private Configuration Space.
  //
  //RegOffset  = 0x3414;               // RTC Backed Up Control (BUC) offset
  //RtcPortId  = 0xC3;                 // RTC port ID
  P2sbBase   = MmPciBase (0, PCI_DEVICE_NUMBER_PCH_LPC, 1); // P2SB device base

  P2sbIsHidden = FALSE;
  if (MmioRead16 (P2sbBase) == 0xFFFF) {
    MmioWrite8 (P2sbBase + R_P2SB_CFG_E0 + 1, 0); // unhide H2SB
    P2sbIsHidden = TRUE;
    DEBUG ((DEBUG_VERBOSE, "P2sb is hidden, unhide it\n"));
  }

  P2sbBar   = MmioRead32 (P2sbBase + R_P2SB_CFG_SBREG_BAR);
  P2sbBar  &= 0xFFFFFFF0;
  ASSERT (P2sbBar != 0xFFFFFFF0);

  Data32    = MmioRead32 (P2sbBar | ((PID_RTC_HOST) << 16) | (UINT16)(R_RTC_PCR_BUC));
  DEBUG ((DEBUG_VERBOSE, "--P2sbBar=0x%x, Data32=0x%x\n", P2sbBar, Data32));

  if (P2sbIsHidden) {
    MmioWrite8 (P2sbBase + R_P2SB_CFG_E0 + 1, BIT0); //Hide P2SB
    DEBUG ((DEBUG_VERBOSE, "Hide p2sb again.\n"));
  }

  if ((Data32 & BIT0) == 0) {
    DEBUG ((DEBUG_VERBOSE, "Boot from partition 1\n"));
    *BootPartition = BootPartition1;
  } else {
    DEBUG ((DEBUG_VERBOSE, "Boot from partition 2\n"));
    *BootPartition = BootPartition2;
  }

  return EFI_SUCCESS;
}


/**
  Board specific hook points.

  Implement board specific initialization during the boot flow.

  @param[in] InitPhase             Current phase in the boot flow.

**/
VOID
BoardInit (
  IN  BOARD_INIT_PHASE  InitPhase
)
{
  BOOT_PARTITION_SELECT     BootPartition;
  EFI_STATUS                Status;
  UINT8                     DebugPort;

  //  0xFF: External 0x3F8 based I/O UART
  // 0,1,2: Internal SOC MMIO UART
  DebugPort = 0xFF;

  switch (InitPhase) {
  case PostTempRamInit:
    DisableWatchDogTimer ();
    SetDebugPort (DebugPort);
    if ((DebugPort != 0xFF) && (DebugPort < PCH_MAX_SERIALIO_UART_CONTROLLERS)) {
      GpioConfigurePads (2, mUartGpioTable + (DebugPort << 1));
    }
    PlatformHookSerialPortInitialize ();
    SerialPortInitialize ();
    Status = GetBootPartition (&BootPartition);
    if (!EFI_ERROR(Status)) {
      SetCurrentBootPartition (BootPartition == BootPartition2 ? 1 : 0);
    }
    break;
  default:
    break;
  }
}

/**
  Get size of Platform Specific Data

  @param[in] none

  @retval    UINT32     Size of Platform Specific Data

**/
UINT32
EFIAPI
GetPlatformDataSize (
  IN  VOID
  )
{
  return sizeof (PLATFORM_DATA);
}

