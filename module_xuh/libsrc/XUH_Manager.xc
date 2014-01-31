

#include <xs1.h>
#include <print.h>
#include <platform.h>
#define ARCH_S 1


#ifdef ARCH_S
#include "xa1_registers.h"
#include "glx.h"
#include <xs1_su.h>
#endif

#ifndef XUH_MAX_EPS
#define XUH_MAX_EPS 16
#endif

#include "xuh_support.h"

XUX_chan epChans[XUH_MAX_EPS * 2];
XUX_chan epChans0[XUH_MAX_EPS* 2];

typedef struct XUH_ep_info
{
    unsigned int chan_array_ptr;        // 0 Pointer into epChans (could be 0)
    unsigned int ep_xud_chanend;        // 1 Chanend ID
    unsigned int ep_client_chanend;     // 2 Destination of chanend (client uses this)
    unsigned int token;                 // 3 Token to tx i.e. SETUP, OUT, IN
    unsigned int data_pid;              // 4 Data packet pid
    unsigned int buffer_addr;           // 5 Data packet pid
} XUH_ep_info;

XUH_ep_info ep_info[XUH_MAX_EPS * 2];

extern unsigned char crc5Table[1024*2];

//unsigned sofCounter = 0x41f1a5;
unsigned sofCounter = 1;

/* Address we will give device */
unsigned devAddr = 0;

/* TODO get this from usb_defs.h */
#define USB_PIDn_OUT                    0xe1
#define USB_PIDn_IN                     0x69
#define USB_PIDn_SOF                    0xa5
#define USB_PIDn_SETUP                  0x2d

unsigned Token_Setup[XUH_MAX_EPS];
unsigned Token_In[XUH_MAX_EPS];
unsigned Token_Out[XUH_MAX_EPS];

/* CRC[23:19], EP[18:15], Addr[14:8], PID[7:0] */ 
static void GenerateTokens()
{
    for(int i = 0; i<XUH_MAX_EPS; i++) 
    {
        unsigned data = (i << 7) | devAddr;
        unsigned char  crc5 = crc5Table[data];
        
        Token_Setup[i] = (crc5 << 19) | (data << 8) | USB_PIDn_SETUP;   
        Token_Out[i] = (crc5 << 19) | (data << 8) | USB_PIDn_OUT;   
        Token_In[i] = (crc5 << 19) | (data << 8) | USB_PIDn_IN;   
    }
}

void XUH_IoLoop(XUX_chan epChans[]);

extern unsigned get_tile_id(tileref ref);
int write_periph_word(tileref tile, unsigned peripheral, unsigned addr, unsigned data)
{
    unsigned tmp[1];
    tmp[0] = data;
    return write_periph_32(tile, peripheral, addr, 1, tmp);
}

int read_periph_word(tileref tile, unsigned peripheral, unsigned addr, unsigned &data)
{
    unsigned tmp[1];
    int retval = read_periph_32(tile, peripheral, addr, 1, tmp);
    data = tmp[0];
    return retval;
}
#ifdef ARCH_S
//#warning BUILDING FOR ARCH_S
/* USB Port declarations - for Zevious with Galaxion */
out port tx_readyout = XS1_PORT_1K; // aka txvalid
in port tx_readyin = XS1_PORT_1H;
out buffered port:32 p_usb_txd = XS1_PORT_8A;
in buffered port:32 p_usb_rxd = XS1_PORT_8C;
in port rx_rdy = XS1_PORT_1M;
in port flag0_port = XS1_PORT_1N;
in port flag1_port = XS1_PORT_1O;
in port flag2_port = XS1_PORT_1P;
buffered in port:1 p_usb_clk = XS1_PORT_1J;
clock tx_usb_clk = XS1_CLKBLK_5;
clock rx_usb_clk = XS1_CLKBLK_4;
#define reg_write_port null
#define reg_read_port null
#else
#error ARCHS OTHER THAN _S NOT DONE YET!
/* USB Port declarations */
extern in port  p_usb_clk       ;
extern out port reg_write_port  ;
extern in  port reg_read_port   ;
extern in  port flag0_port      ;
extern in  port flag1_port      ;
extern in  port flag2_port      ;
extern out port p_usb_txd       ;
extern port p_usb_rxd       ;
#endif

/* Wait for device to be detected and return speed */
#define STATE_IDLE 0
#define STATE_VP   1
#define STATE_VM   2

// 100ms debounce
#define CONNECT_DEBOUNCE (100000 * 100)

extern tileref usb_tile;
#define xs1_su usb_tile
#define USB_TILE_REF usb_tile
void XUH_Manager(chanend c_ep_out[], unsigned epChanCount_out, 
                 chanend c_ep_in[], unsigned epChanCount_in)
{
    timer t;
#define sof_timer t
  int state = STATE_IDLE;
    int nextState;
    unsigned time;
    unsigned int settings[1];
    int retVal = -1;

    GenerateTokens();

    for(int i = 0; i < epChanCount_out; i++)
    {
        int x;
        /* Get resource ID of channel */
        asm("mov %0, %1" : "=r"(epChans0[i]) : "r"(c_ep_out[i]));

        asm("ldaw %0, %1[%2]":"=r"(x):"r"(epChans),"r"(i));
        ep_info[i].chan_array_ptr = x;

        asm("mov %0, %1":"=r"(x):"r"(c_ep_out[i]));
        ep_info[i].ep_xud_chanend = x;
  
        asm("getd %0, res[%1]":"=r"(x):"r"(c_ep_out[i]));
        ep_info[i].ep_client_chanend = x;      
  
        /* Load memmory address */	  
        asm("ldaw %0, %1[%2]":"=r"(x):"r"(ep_info),"r"(i*sizeof(XUH_ep_info)/sizeof(unsigned)));
        
        /* Send memory address of EP struct over channel */
        outuint(c_ep_out[i], x);
    }
    
    for(int i = 0; i < epChanCount_in; i++)
    {
        int x;
        /* Get resource ID of channel */
        asm("mov %0, %1" : "=r"(epChans0[i+XUH_MAX_EPS]) : "r"(c_ep_in[i]));

        asm("ldaw %0, %1[%2]":"=r"(x):"r"(epChans),"r"(i+XUH_MAX_EPS));
        ep_info[i+XUH_MAX_EPS].chan_array_ptr = x;

        asm("mov %0, %1":"=r"(x):"r"(c_ep_in[i]));
        ep_info[XUH_MAX_EPS+i].ep_xud_chanend = x;

        asm("getd %0, res[%1]":"=r"(x):"r"(c_ep_in[i]));
        ep_info[XUH_MAX_EPS+i].ep_client_chanend = x;
        
        /* Load memmory address */	  
        asm("ldaw %0, %1[%2]":"=r"(x):"r"(ep_info),"r"((i+XUH_MAX_EPS)*sizeof(XUH_ep_info)/sizeof(unsigned)));
        
        /* Send memory address of EP struct over channel */
        outuint(c_ep_in[i], x);
    }


    /* Make sure ports are on and reset port states */
    set_port_use_on(p_usb_clk);
#ifndef ARCH_S
    set_port_clock(p_usb_clk, clk);
#endif
    set_port_use_on(p_usb_txd);
    set_port_use_on(p_usb_rxd);
    set_port_use_on(flag0_port);
    set_port_use_on(flag1_port);
    set_port_use_on(flag2_port);
#ifndef ARCH_S
    set_port_use_on(reg_read_port);
    set_port_use_on(reg_write_port);
#endif

#define TX_RISE_DELAY 5
#define TX_FALL_DELAY 2
#define RX_RISE_DELAY 5
#define RX_FALL_DELAY 5


 // Set up USB ports. Done in ASM as read port used in both directions initially.
  // Main difference from xevious is IFM not enabled.
  // GLX_UIFM_PortConfig (p_usb_clk, txd, rxd, flag0_port, flag1_port, flag2_port);
  // Xevious needed asm as non-standard usage (to avoid clogging 1-bit ports)
  // GLX uses 1bit ports so shouldn't be needed.
  // Handshaken ports need USB clock
  configure_clock_src (tx_usb_clk, p_usb_clk);
  configure_clock_src (rx_usb_clk, p_usb_clk);
  
  //this along with the following delays forces the clock 
  //to the ports to be effectively controlled by the 
  //previous usb clock edges
  set_port_inv(p_usb_clk);
  set_port_sample_delay(p_usb_clk);

  //this delay controls the capture of rdy
  set_clock_rise_delay(tx_usb_clk, TX_RISE_DELAY);


  set_clock_fall_delay(tx_usb_clk, TX_FALL_DELAY);
  
  //this delay th capture of the rdyIn and data. 
  set_clock_rise_delay(rx_usb_clk, RX_RISE_DELAY);
  set_clock_fall_delay(rx_usb_clk, RX_FALL_DELAY);

  	start_clock(tx_usb_clk);
  	start_clock(rx_usb_clk);
 	configure_out_port_handshake(p_usb_txd, tx_readyin, tx_readyout, tx_usb_clk, 0);
  	configure_in_port_strobed_slave(p_usb_rxd, rx_rdy, rx_usb_clk);

    printstr("Enable USB...");
#ifdef ARCH_S
        /* Enable the USB clock */
        write_sswitch_reg(get_tile_id(USB_TILE_REF), XS1_GLX_CFG_RST_MISC_ADRS, ( ( 1 << XS1_GLX_CFG_USB_CLK_EN_BASE ) ) );
        //write_node_config_reg(xs1_su, XS1_GLX_CFG_RST_MISC_ADRS, ( ( 1 << XS1_GLX_CFG_USB_CLK_EN_BASE ) ) );

        /* Now reset the phy */
        write_periph_word(USB_TILE_REF, XS1_GLX_PERIPH_USB_ID, XS1_UIFM_PHY_CONTROL_REG,  (1<<XS1_UIFM_PHY_CONTROL_FORCERESET));

        /* Keep usb clock active, enter active mode */
        write_sswitch_reg(get_tile_id(USB_TILE_REF), XS1_GLX_CFG_RST_MISC_ADRS, (1 << XS1_GLX_CFG_USB_CLK_EN_BASE) | (1<<XS1_GLX_CFG_USB_EN_BASE)  );
#else
// TODO
#endif
   printstrln("ok");
    t :> time;
    printstr("wait for USB Clock..(");
    printint(time);
    printstr(")\n");
    /* Wait for USB clock (typically 1ms after reset) */
    p_usb_clk when pinseq(1) :> int _;
    p_usb_clk when pinseq(0) :> int _;
    p_usb_clk when pinseq(1) :> int _;
    p_usb_clk when pinseq(0) :> int _; 
    t :> time;
    printstr("ok (");
    printint(time);
    printstr(")\n");

    // Turn on pulldowns TODO and VBUS?
    settings[0] = XS1_SU_UIFM_OTG_CONTROL_DPPULLDOWN_SET(0, 1);
    settings[0] = XS1_SU_UIFM_OTG_CONTROL_DMPULLDOWN_SET(settings[0], 1);
    settings[0] = XS1_SU_UIFM_OTG_CONTROL_DRVVBUSEXT_SET(settings[0], 1);
    write_periph_32(xs1_su, XS1_SU_PER_UIFM_CHANEND_NUM, XS1_SU_PER_UIFM_OTG_CONTROL_NUM, 1, settings);

    //device detection mode
    settings[0] = XS1_SU_UIFM_FUNC_CONTROL_OPMODE_SET(0, 0x0); // OPMODE_0
    settings[0] = XS1_SU_UIFM_FUNC_CONTROL_TERMSELECT_SET(settings[0], 1);
    settings[0] = XS1_SU_UIFM_FUNC_CONTROL_XCVRSELECT_SET(settings[0], 1);
    write_periph_32(xs1_su, XS1_SU_PER_UIFM_CHANEND_NUM, XS1_SU_PER_UIFM_FUNC_CONTROL_NUM, 1, settings);
        
    // Ensure line state decoding is disabled FIXME this is just for debug
    // RSO: Enabled linestate decoding to detect chirp
    settings[0] = XS1_SU_UIFM_IFM_CONTROL_DECODELINESTATE_SET(0, 1);
    write_periph_32(xs1_su, XS1_SU_PER_UIFM_CHANEND_NUM, XS1_SU_PER_UIFM_CONTROL_NUM, 1, settings);

    // Write flag masks to UIFM
    settings[0] = /*{*/ XS1_SU_UIFM_FLAGS_MASK_MASK0_SET(0, /*UIFM_IFM_FLAGS_LS0_DP*/0x10) /*}*/;
    settings[0] = XS1_SU_UIFM_FLAGS_MASK_MASK1_SET(settings[0], /*UIFM_IFM_FLAGS_LS1_DM*/0x8);
    write_periph_32(xs1_su, XS1_SU_PER_UIFM_CHANEND_NUM, XS1_SU_PER_UIFM_MASK_NUM, 1, settings);

    printstr("Waiting for device...\n");

  // Check DP/DM states for a device
  while (retVal < 0)
  {
      switch (state)
      {
        case STATE_IDLE:
            printstr("IDLE: ");
            select
            {
                case flag0_port when pinsneq(0) :> void:
                    printstr("STATE_VP\n");
                    nextState = STATE_VP;
                    break;
                case flag1_port when pinsneq(0) :> void:
                    nextState = STATE_VM;
                    printstr("STATE_VM\n");
                    break;
            }
            break;
        case STATE_VP: // Full Speed
            sof_timer :> time;
            time += CONNECT_DEBOUNCE;
            printstr("STATE_VP: ");
            select
            {
                case flag0_port when pinsneq(1) :> void:
                    nextState = STATE_IDLE;
                    printstr("STATE_IDLE\n");
                    break;
                case flag1_port when pinsneq(0) :> void:
                    nextState = STATE_IDLE;
                    printstr("STATE_IDLE\n");
                    break;
                case sof_timer when timerafter(time) :> void:
                    printstr("EXIT\n");
                    retVal = 0;
                    break;
            }
            break;
        case STATE_VM: // Low Speed
            sof_timer :> time;
            time += CONNECT_DEBOUNCE;
            select
            {
                case flag0_port when pinsneq(0) :> void:
                    nextState = STATE_IDLE;
                    break;
                case flag1_port when pinsneq(1) :> void:
                    nextState = STATE_IDLE;
                    break;
                case sof_timer when timerafter(time) :> void:
                    retVal = 1;
                    break;
            }
            break;
        default:
            nextState = STATE_IDLE;
            break;
      }
      state = nextState;
    }

    //if(retVal)
      //  printstr("LOW speed dev detected!\n");

    //else
      ///  printstr("FULL speed dev detected!\n");
    
    // Drive SE0 on the bus (D+ and D- connected to ground via 45ohm resistors)
    // Set opmode to 0b10 for connrect chirp transmit and receive
    // OpMode: 0b10, TermSelect and XcvrSelect 0
    settings[0] = XS1_SU_UIFM_FUNC_CONTROL_OPMODE_SET(0, 0x2);
    settings[0] = XS1_SU_UIFM_FUNC_CONTROL_TERMSELECT_SET(settings[0], 0);
    settings[0] = XS1_SU_UIFM_FUNC_CONTROL_XCVRSELECT_SET(settings[0], 0);
    write_periph_32(xs1_su, XS1_SU_PER_UIFM_CHANEND_NUM, XS1_SU_PER_UIFM_FUNC_CONTROL_NUM, 1, settings);

    /* Wait for chirp K from device to signal high-speed */
    flag1_port when pinseq(1) :> void;

    /* Wait for end of K chirp from device.. */
    flag1_port when pinseq(0) :> void;
    

    /* Chirp back to device */
#define HOST_CHIRP_LENGTH 700
    for(int i = 0; i< 30; i++)
    {   
        for(int j = 0; j < HOST_CHIRP_LENGTH; j++)
         p_usb_txd <: 0xffffffff;
        
        for(int j = 0; j < HOST_CHIRP_LENGTH; j++)
         p_usb_txd <: 0x0;
    }
 
   // printstr("HS!!\n");
    


    /* go back to normal termination */
    //settings[0] = XS1_SU_UIFM_FUNC_CONTROL_OPMODE_SET(0, 0x0); // OPMODE_0
    //settings[0] = XS1_SU_UIFM_FUNC_CONTROL_TERMSELECT_SET(settings[0], 1);
    //settings[0] = XS1_SU_UIFM_FUNC_CONTROL_XCVRSELECT_SET(settings[0], 1);
    //write_periph_32(xs1_su, XS1_SU_PER_UIFM_CHANEND_NUM, XS1_SU_PER_UIFM_FUNC_CONTROL_NUM, 1, settings);

    /* Go into HS operation */
    settings[0] = XS1_SU_UIFM_FUNC_CONTROL_OPMODE_SET(0, 0x0); // OPMODE_0
    settings[0] = XS1_SU_UIFM_FUNC_CONTROL_TERMSELECT_SET(settings[0], 0);
    settings[0] = XS1_SU_UIFM_FUNC_CONTROL_XCVRSELECT_SET(settings[0], 0);
    write_periph_32(xs1_su, XS1_SU_PER_UIFM_CHANEND_NUM, XS1_SU_PER_UIFM_FUNC_CONTROL_NUM, 1, settings);

#if 0 
    /* do some sofs... */
    t :> time;
    //for(int i = 0; i < 100; i++)
    while(1)
    {
        time+=12500;
        t when timerafter(time) :> void;
        partout(p_usb_txd, 24, 0x41f1a5);
    }
#endif

    /* Setup flag ports for the loop.. */
  write_periph_word(USB_TILE_REF, XS1_GLX_PERIPH_USB_ID, XS1_UIFM_FLAGS_MASK_REG,
                ((1<<XS1_UIFM_IFM_FLAGS_NEWTOKEN)
                | ((1<<XS1_UIFM_IFM_FLAGS_RXACTIVE)<<8)
                | ((1<<XS1_UIFM_IFM_FLAGS_RXERROR)<<16)));

    XUH_IoLoop(epChans);
}


/*int main()
{
    printstr("Host test\n");

    hosttest();

}*/
