
#include <xs1.h>


#define USB_PIDn_SOF                    0xa5
#define USB_PIDn_ACK                    0xd2
#define USB_PIDn_NAK                    0x5a

#define XUH_FRAME_TIME                      12500

.section        .cp.const4,"aMc",@progbits,4
.cc_top XUH_FrameTime.data
.align 4
XUH_FrameTime:
.long  XUH_FRAME_TIME
.cc_bottom XUH_FrameTime.data
.text

#define XUH_IOLOOP_STACKEXTEND              32
#define XUH_IOLOOP_STACKFRAME_FRAMETIMER    (XUH_IOLOOP_STACKEXTEND - 8)

.globl XUH_IoLoop.nstackwords
.globl XUH_IoLoop.maxthreads
.globl XUH_IoLoop.maxtimers
.globl XUH_IoLoop.maxchanends

.linkset XUH_IoLoop.nstackwords, XUH_IOLOOP_STACKEXTEND
.linkset XUH_IoLoop.maxchanends, 0
.linkset XUH_IoLoop.maxtimers, 1
.linkset XUH_IoLoop.maxthreads, 0

.globl XUH_IoLoop
.cc_top XUH_IoLoop.func, XUH_IoLoop
.text
XUH_IoLoop:
    entsp       XUH_IOLOOP_STACKEXTEND 
    stw         r4, sp[1]
    stw         r5, sp[2]
    stw         r6, sp[3]
    stw         r7, sp[4]
    stw         r8, sp[5]
    stw         r9, sp[6]
    stw         r10, sp[7]
    
    clre
XUH_SetupRxdPortEvent:
    ldw         r9, dp[p_usb_rxd]
    ldap        r11, XUH_NextInWord                                            
    setd        res[r9], r11

XUH_SetupRxAPortEvent:
    ldw         r9, dp[flag1_port]
    setc        res[r9], XS1_SETC_COND_EQ
    setc        res[r9], XS1_SETC_IE_MODE_EVENT
    ldc         r11, 0
    setd         res[r9], r11
    ldap        r11, XUH_RxALow
    setv        res[r9], r11
    //eeu         res[r9]

XUH_SetupFrameTimer:
    getr        r10, XS1_RES_TYPE_TIMER
    ecallf      r10
    
    ldap        r11, XUH_DoFrame
    setv        res[r10], r11

    setc        res[r10], XS1_SETC_COND_NONE
    in          r11, res[r10]                    // Get current time
    ldw         r9, cp[XUH_FrameTime]
    add         r11, r11, r9
    setd        res[r10], r11
   
    setc        res[r10], XS1_SETC_COND_AFTER

    stw         r10, sp[XUH_IOLOOP_STACKFRAME_FRAMETIMER]

    eeu         res[r10]      

XUH_SetupForLoop:
    mov         r5, r0              // r5: epChans

    ldw         r0, dp[p_usb_rxd]   // r0: RXD
    ldw         r1, dp[p_usb_txd]   // r1: TXD
    mov         r2, r10             // r2: timer

    waiteu
    
XUH_DoFrame:
    //ldw         r11, dp[p_usb_txd]
    ldw         r8, dp[sofCounter]

XUH_GenerateSof:

    //eq          r4, r8, 10
    //bt          r4, SetupTest

    ldc         r7, 0x3ff8              // Note, only update frame-number every 8 microframes.
    and         r6, r7, r8              // Mask off 11 bits for frame counter
    shr         r6, r6, 3
    ldaw        r3, dp[crc5Table]       // get CRC5 for framecounter
    ld8u        r3, r3[r6]
    shl         r3, r3, 16              // TODO load this shifted
    shl         r3, r3, 3
    //shr         r6, r6, 4
    shl         r6, r6, 8

    ldc         r7, USB_PIDn_SOF        // Or in PID
    or          r7, r7, r6  
    or          r7, r7, r3              // On in frame number            

    outpw       res[r1], r7, 24        // Output SOF
    syncr       res[r1]

    add         r8, r8, 1               // Increment frame counter  // TODO Only increment every 8 times
    stw         r8, dp[sofCounter]      // Store back SOF counter

                                        // Check what each ep wants to do..
XUH_SetupNextFrameTime:
    in          r11, res[r2]           // Setup timer event for next frame
    ldw         r9, cp[XUH_FrameTime]                     
    add         r11, r11, r9
    setd        res[r2], r11

XUH_CheckEPs:
    // Go through ep table and see if any want service
    // TODO can we think of a better scheme than this?
    ldw        r4, r5[0]                       
    bt         r4, EpReady_out 
    ldw        r4, r5[1]                       
    bt         r4, EpReady_out                   
    ldw        r4, r5[2]                       
    bt         r4, EpReady_out                   
    ldc        r7, 16
    ldw        r4, r5[r7]                       
    bt         r4, EpReady_In   

    ldc        r7, 17
    ldw        r4, r5[r7]                       
    bt         r4, EpReady_In  

    ldc        r7, 18
    ldw        r4, r5[r7]                       
    bt         r4, EpReady_In  

     ldc        r7, 19
    ldw        r4, r5[r7]                       
    bt         r4, EpReady_In               

XUH_WaitForNextFrame:
    waiteu


.align 64
.skip 34
EpReady_out:                        
    // EP is ready, 
    ldc     r9, 0xa001              // CRC poly
    ldc     r7, 0xf335              // TX CRC init
    
EpReady_LoadToken:
    ldw     r11, r4[3]  

EpReady_OutputToken:
    outpw   res[r1], r11, 24
    syncr   res[r1]
 
EpReady_LoadPid:
    ldw     r11, r4[4]  
    
    ldw     r8, r4[5]               // Load buffer
    ldw     r6, r4[7]               // Load tail length (bytes)

    ldw     r3,  r4[6]               // Load buffer index (negative)
    bf      r3, XUH_SmallOutTransfer

    bru     r6
  
.align 64
.skip 0
#define TXD r1
TxTail0:
    ldw         r6, r8[r3]                      // Load first data word
    add         r3, r3, 1                       // Buffer index increment
XUD_IN_TxPid_Tail0:
    outpw       res[TXD], r11, 8                // Output PID to port
    out         res[TXD], r6                    // Output first data word
    crc32       r7, r6, r9                      // Run CRC
    bf          r3, TxLoop0End
TxLoop0:
    ldw         r6, r8[r3]                      // Load last data word
    add         r3, r3, 1
TxLoop0_Out:
    out         res[TXD], r6                    // Output last data word
    crc32       r7, r6, r9
    bt          r3, TxLoop0
TxLoop0End:                                     // Word aligned data (just output CRC)
    crc32       r7, r3, r9                      // R4: 0 (from bt)
    not         r7, r7

XUD_IN_TxCrc_Tail0:
    outpw        res[TXD], r7, 16               // Output CRC
    bu           DoneTail

.align 64
.skip 0
TxTail1:
    ldw         r6, r8[r4]                      // Load first data word
    add         r4, r4, 1                       // Buffer index increment
    // in       r6, res[r5]                     // Get data word
    // testct   r8, res[r5]
XUD_IN_TxPid_Tail1:
    outpw    res[TXD], r11, 8                   // Out PID
    out      res[TXD], r6
    crc32         r7, r6, r9
    //testct   r8, res[r5]
    bf            r4, TxLoop1End
TxLoop1:
    //in       r6, res[r5]                      // Get data word
    //testct   r11, res[r5]
    ldw         r6, r8[r4]                      // Load last data word
    add         r4, r4, 1
    out         res[TXD], r6
    crc32       r7, r6, r9
    bt          r4, TxLoop1
TxLoop1End:
    //inct        r6, res[r5]                     // Input end of stream token
    //in          r6, res[r5]                     // Input tail data
    ldw         r6, r8[r4]                      // Load last data word
    outpw       res[TXD], r6, 8
    crc8        r7, r6, r6, r9
    crc32       r7, r4, r9                      // r4: 0 (from bf)
    not         r7, r7

XUD_IN_TxCrc_Tail1:
    outpw        res[TXD], r7, 16
    bu           DoneTail

.scheduling off
.align 64
.skip 0
TxTail2:
    ldw         r6, r8[r4]                        // Load first data word
    crc32       r7, r6, r9
XUD_IN_TxPid_Tail2:
    outpw       res[TXD], r11, 8                   // Out PID
    out         res[TXD], r6
    add         r4, r4, 1
    bf          r4, TxLoop2End
TxLoop2:
    ldw         r6, r8[r4]                        // Load first data word
    add         r4, r4, 1
    crc32       r7, r6, r9
    out         res[TXD], r6
    bt          r4, TxLoop2

TxLoop2End:
    ldw         r6, r8[r4]                      // Load tail
    crc8        r7, r11, r6, r9
    outpw       res[TXD], r6, 16
    crc8        r7, r11, r11, r9
    crc32       r7, r4, r9                      // r4: 0 (from bf)
    not         r7, r7

XUD_IN_TxCrc_Tail2:
    outpw       res[TXD], r7, 16
    bu          DoneTail

.align 64
.skip 0
TxTail3:
    ldw         r6, r8[r3]                      // Load first data word
    add         r3, r3, 1
XUD_IN_TxPid_Tail3:
    outpw    res[TXD], r11, 8                   // Out PID
    out      res[TXD], r6
    crc32      r7, r6, r9
    bf         r3, TxLoop3End
TxLoop3:
    ldw         r6, r8[r3]                      // Load first data word
    add         r3, r3, 1
    crc32       r7, r6, r9
    out         res[TXD], r6
    bt          r3, TxLoop3
TxLoop3End:
    ldw          r6, r8[r3]                      // Load tail
    crc8         r7, r11, r6, r9
    outpw        res[TXD], r6, 24
    crc8         r7, r11, r11, r9
    crc8         r7, r11, r11, r9
    crc32        r7, r3, r9                // r4: 0 (from bf)
    not          r7, r7
XUD_IN_TxCrc_Tail3:
    outpw        res[TXD], r7, 16

DoneTail:
     
    inpw    r11, res[r0], 8         // TODO: Timeout
                                    // TODO: Check for valid ACK (could be STALL or NAK or NYET)

    shr     r11, r11, 24
    ldc     r9, USB_PIDn_NAK        // TODO rather than checking for not NACK, check for PID expected?
    eq      r9, r9, r11
    bt      r9, XUH_OutNak


    //ldc     r11, 0                // TODO CURRENTLY clearing EP0?
    //stw     r9, r5[0]               // Clear ready 
    ldw     r11, r4[0]              // note, potentially wasted 1 inst here?
    stw     r9, r11[0]
    ldw     r11, r4[1]              // load chanend
    out     res[r11], r11

XUH_OutNak:
    //bu XUH_SetupNextFrameTime
    waiteu


.align 32
.skip 28
XUH_SmallOutTransfer:
SmallTxPacket:
    ldw          r8, r8[r3]                        // Load first data word
    
    .xtabranch TxTail0s, TxTail1s, TxTail2s, TxTail3s
    bru         r6                              // branch taillength

//--------------------------------------------------------------------------
.align 64
.skip 0
TxTail0s:                                       // We know this is a < 4 byte packet, so is 0 length packet
                                                // So crc = 0. Note our normal crc calculation works for this, it is
                                                //not a special CRC case, but helps with timing.
    ldc        r8, 0
XUD_IN_TxPid_TailS0:
    outpw      res[TXD], r11, 8                 // PID
    //crc32    r7, r11, r9                      // r11: 0
    //not      r7, r7

XUD_IN_TxCrc_TailS0:
    outpw      res[TXD], r8, 16                 // r5: 0
    nop
    nop
    nop
    bu         DoneTail

.align 64
.skip 0
TxTail1s:
    ldc        	r6, 0
    // One tail byte
    shl 		r8, r8, 8                       // Concat PID and Data
    or  		r11, r11, r8
    shr 		r8, r8, 8
    crc8        r7, r4, r8, r9
    crc32       r7, r6, r9               		// r5: 0
    not         r7, r7

XUD_IN_TxPid_TailS1:
    outpw     	res[TXD], r11, 16        		// PID
    ///outpw       res[TXD], r6, 16
XUD_IN_TxCrc_TailS1:
    outpw       res[TXD], r7, 16
    bu           DoneTail

.align 64
.skip 0
TxTail2s:
    ldc        r6, 0
    // Two tail byte
    shl         r8, r8, 8
    or          r11, r11, r8
    shr         r8, r8, 8
    //outpw     res[TXD], r11, 8        // PID
    //outpw        res[TXD], r6, 16

    crc8         r7, r8, r8, r9
    crc8         r7, r8, r8, r9
    crc32        r7, r6, r9               // r5: 0
    not          r7, r7
    shr          r6, r7, 8

    shl        r7, r7, 24
    zext       r11, 24
    or         r11, r11, r7
XUD_IN_TxPid_TailS2:
    out     res[TXD], r11
//    outpw        res[TXD], r7, 16

XUD_IN_TxCrc_TailS2:
    outpw      res[TXD], r6, 8
    bu           DoneTail

.align 64
.skip 0
TxTail3s:
    ldc        r6, 0
	// Two tail byte
XUD_IN_TxPid_TailS3:
  outpw        res[TXD], r11, 8         // PID
  outpw        res[TXD], r8, 24
  crc8         r7, r8, r8, r9
  crc8         r7, r8, r8, r9
  crc8         r7, r8, r8, r9

  crc32        r7, r6, r9               // r5: 0
  not          r7, r7

XUD_IN_TxCrc_TailS3:
  outpw        res[TXD], r7, 16
  bu           DoneTail

////////////////////

EpReady_In:
    ldw     r11, r4[3]              // Load token
    ldw     r6, r4[5]               // Load buffer

EpReady_In_OutputToken:
    outpw   res[r1], r11, 24
    syncr   res[r1]
   
    inpw    r9, res[r0], 8        // TODO: Timeout
   
    shr     r9, r9, 24
    ldc     r8, USB_PIDn_NAK     // TODO rather than checking for not NACK, check for PID expected?
    eq     r9, r9, r8
    bf      r9, GotData
     
    waiteu
////////////////////////

.align 32
.skip 4
GotData:
                                    // r9 is 0 from bf above
    ldw     r11, dp[flag1_port]     // enable events on RXA
    eeu     res[r11]
    setsr   1                       // Enable thread events

XUH_NextInWord:                     // Rx loop
    in      r10, res[r0]
    stw     r10, r6[r9] 
    add     r9, r9, 1
                                    // TODO: CRC 
    bu      XUH_NextInWord

XUH_RxALow:                         // Framing signal low 
    clre                            // TODO deal with tail!
    in      r11, res[r11]
    endin   r8, res[r0]
    in      r11, res[r0]

    bru     r8

.align 16
OutTail0:   
    nop

.align 16
OutTail1:
    shr     r8, r8, 24
    stw     r11, r6[r9] 

.align 16
OutTail2:
    shr     r8, r8, 16
    stw     r11, r6[r9] 

.align 16
OutTail3:
    shr     r8, r8, 8
    stw     r11, r6[r9] 

    ldc     r11, USB_PIDn_ACK        // TODO check CRC
    outpw   res[r1], r11, 8          // Output ACK:
 
    ldc     r10, 0                  // TODO in from RXA to get this 0?
    ldc     r11, 16                 // TODO, hardcoded ep0
    stw     r10, r5[r11]            // Clear ready   res[r11]
   
    ldw     r11, r4[1]              // Load EP chanend
    out     res[r11], r9            // Output length (words)
    out     res[r11], r8            // Outout tail length (bits)
    eeu     res[r2]                 // Re-enable events on timer
    waiteu                          // Back to SOF generation

XUH_IoLoop_Exit:
    bu      -1
    ldw        r4, sp[1]            // Reg restore
    ldw        r5, sp[2]
    ldw        r6, sp[3]
    ldw        r7, sp[4]
    ldw        r8, sp[5]
    ldw        r9, sp[6]
    ldw        r10, sp[7]

    //TODO FREE TIMERS

    retsp    XUH_IOLOOP_STACKEXTEND

.cc_bottom XUH_IoLoop.func
