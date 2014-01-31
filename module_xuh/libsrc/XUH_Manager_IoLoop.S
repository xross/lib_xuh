
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

    ldc         r7, 0x3ff8               // TODO mkmsk
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
    ldw        r4, r5[0]                       
    bt         r4, EpReady_out                   
    ldc        r7, 16
    ldw        r4, r5[r7]                       
    bt         r4, EpReady_In                   

XUH_WaitForNextFrame:
    waiteu


//r
//r10: sof timer
//r11: tx port
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
    
    ldw     r8, r4[5]               // load buffer
   
    outpw   res[r1], r11, 8         // Output data packet PID
    ldw     r6, r8[0]               // Output first data word 
    out     res[r1], r6
    crc32   r7, r6, r9

    ldw     r6, r8[1]               // Output second data word 
    crc32   r7, r6, r9
    out     res[r1], r6
    ldc     r6, 0
    crc32   r7, r6, r9              // CRC 0 step
    not     r7, r7
    outpw   res[r1], r7, 16

     
    inpw    r11, res[r0], 8        // TODO: Timeout
                                    // TODO: Check for valid ACK (could be STALL or NAK)

    ldc     r11, 0
    stw     r11, r5[0]              // Clear ready
    ldw     r11, r4[1]              // load chanend
    out     res[r11], r11

    //bu XUH_SetupNextFrameTime
    waiteu

//    waiteu

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

GotData:
                                    // r9 is 0 from bf above
    ldw     r11, dp[flag1_port]     // enable events on RXA
    eeu     res[r11]
    setsr   1                       // Enable thread events

XUH_NextInWord:                     // Rx loop
    in      r11, res[r0]
    stw     r11, r6[r9] 
    add     r9, r9, 1
                                    // TODO: CRC 
    bu      XUH_NextInWord

XUH_RxALow:                         // Framing signal low 
    clre                            // TODO endin
    ldc     r8, USB_PIDn_ACK        // TODO check CRC
    outpw   res[r1], r8, 8          // Output ACK:
 
    ldc     r10, 0                  // TODO in from RXA to get this 0?
    ldc     r11, 16                 // TODO, hardcoded ep0
    stw     r10, r5[r11]            // Clear ready   res[r11]
   
    ldw     r11, r4[1]              // Load EP chanend
    out     res[r11], r9            // Output length (words)
                                    // TODO output tail (bits)
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
