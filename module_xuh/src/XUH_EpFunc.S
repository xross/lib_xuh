
#include "usb_defs.h"

// int XUH_OutTansfer(XUD_Ep ep, unsigned char buffer[], unsigned length, int isSetup)
.globl XUH_TxTransfer
.type XUH_TxTransfer, @function
.text
.cc_top XUH_TxTransfer.func
XUH_TxTransfer:
    entsp       7
    stw         r4, sp[0]
    stw         r5, sp[5]
    stw         r10, sp[6]


XUH_TxTransferRetry:

    bt         r3, XUH_TxTransfer_StoreToken_Setup
    ldaw       r11, dp[Token_Out]
    bu         XUH_TxTransfer_StoreToken

XUH_TxTransfer_StoreToken_Setup:
    ldaw       r11, dp[Token_Setup]

XUH_TxTransfer_StoreToken:
    ldw         r11, r11[0]                     // TODO - USE EP NUMBER!!
    
    stw         r11, r0[3]                      // Store token
//XUH_SetupTransfer_NoReq:
  //  add         r1, r1, r3                    // Add start index to buffer address

XUH_TxTransfer_CalcTailLength:
    shl         r3, r2, 5                       // Taillength: bytes to bits * 2
    zext        r3, 7

XUH_TxTransfer_WordLength:
    shr         r2, r2, 2                       // r2: datalength (bytes) ---> datalength (words)

//AdjustBufferPointer:
    shl         r5, r2, 2                       // Get end off buffer address
    add         r1, r1, r5

//NegativeIndex:                                // Produce negtive offset from end of buffer
    neg         r2, r2
    stw         r2, r0[6]                       // Store index

XUH_TxTransfer_DataRdy:
    ldw         r2, r0[0]                       // Load mem address of EP structure
    stw         r1, r0[5]                       // Store buffer address
//StoreTailLength:
    stw         r3, r0[7]                       // Store tail length (bytes)



    stw         r0, r2[0]                       // Mark EP ready with address own EP structure

                                                // Wait for XUH Response
    ldw         r10, r0[2]                      // Load our chanend ID to use
   // testct      r11, res[r10]                   // Test for RESET/SUSPEND exception
   // bt          r11, XUH_SetupTransfer_Reset

    in          r11, res[r10]                   // Data sent okay

//XUH_SetupTransfer_LoadEpType:
  //  ldw         r11, r0[5]                      // Don't do any toggling for ISO
   // bf          r11, XUH_SetupTransfer_DonePid

XUH_TxTransfer_PidToggle:                          //
    ldw         r11, r0[4]                      // Load EP PID from structure
    ldc         r4, 0x88
    xor         r11, r11, r4
    stw         r11, r0[4]                      // Store back PID

XUH_SetupTransfer_DonePid:
    ldc         r0, 0
XUH_TxTransfer_Return:
    ldw         r4, sp[0]
    ldw         r5, sp[5]
    ldw         r10, sp[6]
    retsp       7

XUH_TxTransfer_Reset:
    mkmsk       r0, 32                           // Return -1
    bu          XUH_TxTransfer_Return

.size XUH_TxTransfer, .-XUH_TxTransfer
.cc_bottom XUH_TxTransfer.func
.globl XUH_TxTransfer.nstackwords
.globl XUH_TxTransfer.maxchanends
.globl XUH_TxTransfer.maxtimers
.globl XUH_TxTransfer.maxcores
.set XUH_TxTransfer.nstackwords, 7
.set XUH_TxTransfer.maxchanends, 0
.set XUH_TxTransfer.maxtimers, 0
.set XUH_TxTransfer.maxcores, 1


#if 0
.globl XUH_SetupTransfer
.type XUH_SetupTransfer, @function
.text
.cc_top XUH_SetupTransfer.func
XUH_SetupTransfer:
    entsp       7
    stw         r4, sp[0]
    stw         r5, sp[5]
    stw         r10, sp[6]


XUH_SetupTransferRetry:


//XUH_SetupTransfer_NoReq:
  //  add         r1, r1, r3                      // Add start index to buffer address

//CalcTailLength:
  //  shl         r3, r2, 5                       // Taillength: bytes to bits * 2
   // zext        r3, 7

//SetupLoopTerm:
  //  shr         r2, r2, 2                       // r2: datalength (bytes) ---> r2: datalength (words)

//AdjustBufferPointer:
  //  shl         r5, r2, 2                       // Get end off buffer address
  //  add         r1, r1, r5

//NegativeIndex:                                  // Produce negtive offset from end of buffer
  //  neg         r2, r2
   // stw         r2, r0[6]                       // Store index

XUH_SetupTransfer_DataRdy:
   ldw         r2, r0[0]                       // Load mem address of EP structure
   //stw         r1, r0[3]                       // Store buffer address
//StoreTailLength:
 //   stw         r3, r0[7]                       // Store tail length (bytes)

XUH_SetupTransfer_StoreToken:
   // ldc         r10, PIDn_SETUP
    ldaw        r11, dp[Token_Setup]
    ldw         r11, r11[0]                         // TODO - USE EP NUMBER!!
    
    stw         r11, r0[3]                          // Store token

    ldc         r11, USB_PIDn_DATA0
    stw         r11, r0[4]                          // Store data PID

    stw         r1, r0[5]                       // Store buffer addr

    stw         r0, r2[0]                       // Mark ready with address of ep structure

                                                // Wait for XUD Response
    ldw         r10, r0[2]                      // Load our chanend ID to use
   // testct      r11, res[r10]                   // Test for RESET/SUSPEND exception
   // bt          r11, XUH_SetupTransfer_Reset

    in          r11, res[r10]                   // Data sent okay

//XUH_SetupTransfer_LoadEpType:
  //  ldw         r11, r0[5]                      // Don't do any toggling for ISO
   // bf          r11, XUH_SetupTransfer_DonePid

//XUH_SetupTransfer_PidToggle:                          //
  //  ldw         r11, r0[4]                      // Load EP PID from structure
   // ldc         r4, 0x88
   // xor         r11, r11, r4
   // stw         r11, r0[4]                      // Store back PID

//XUH_SetupTransfer_DonePid:
 //   ldc         r0, 0
XUH_SetupTransfer_Return:
    ldw         r4, sp[0]
    ldw         r5, sp[5]
    ldw         r10, sp[6]
    retsp       7

XUH_SetupTransfer_Reset:
    mkmsk       r0, 32                           // Return -1
    bu          XUH_SetupTransfer_Return

.size XUH_SetupTransfer, .-XUH_SetupTransfer
.cc_bottom XUH_SetupTransfer.func
.globl XUH_SetupTransfer.nstackwords
.globl XUH_SetupTransfer.maxchanends
.globl XUH_SetupTransfer.maxtimers
.globl XUH_SetupTransfer.maxcores
.set XUH_SetupTransfer.nstackwords, 7
.set XUH_SetupTransfer.maxchanends, 0
.set XUH_SetupTransfer.maxtimers, 0
.set XUH_SetupTransfer.maxcores, 1
#endif

.globl XUH_InTransfer
.type XUH_InTransfer, @function
.text

.cc_top XUH_InTransfer.func
XUH_InTransfer:
    entsp       4
    stw         r4, sp[1]
    stw         r10, sp[2]

    ldw        r4, r0[0]                        // Load mem address of EP structure
    stw        r1, r0[5]                        // Store buffer addr

    ldaw        r11, dp[Token_In]
    ldw         r11, r11[0]                         // TODO - USE EP NUMBER!!
    
    stw         r11, r0[3]                          // Store token
    

    stw        r0, r4[0]                        // mark ready
    ldw        r10, r0[2]                       // Load channel for us to use

    in         r0, res[r10]                  // get length over channel
    
    shl        r0, r0, 2

XUH_InTransfer_ReturnOk:
    ldw         r10, sp[2]
    ldw         r4, sp[1]
    retsp       4



.size XUH_InTransfer, .-XUH_InTransfer
.cc_bottom XUH_InTransfer.func
.globl XUH_InTransfer.nstackwords
.globl XUH_InTransfer.maxchanends
.globl XUH_InTransfer.maxtimers
.globl XUH_InTransfer.maxcores
.set XUH_InTransfer.nstackwords, 4
.set XUH_InTransfer.maxchanends, 0
.set XUH_InTransfer.maxtimers, 0
.set XUH_InTransfer.maxcores, 1
