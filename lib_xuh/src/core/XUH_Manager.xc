

#include <xs1.h>
#include <print.h>
#include <platform.h>
//#define ARCH_S 1


#ifdef ARCH_S
#include "xa1_registers.h"
#include "glx.h"
#endif
#include <xs1_su.h>

#include "xud.h"

#ifndef XUH_MAX_EPS
#define XUH_MAX_EPS 16
#endif

#include "xuh_support.h"

XUX_chan xuh_epChans[XUH_MAX_EPS * 2];
XUX_chan xuh_epChans0[XUH_MAX_EPS* 2];

typedef struct XUH_ep_info
{
    unsigned int chan_array_ptr;        // 0 Pointer into xuh_epChans (could be 0)
    unsigned int ep_xud_chanend;        // 1 Chanend ID
    unsigned int ep_client_chanend;     // 2 Destination of chanend (client uses this)
    unsigned int token;                 // 3 Token to tx i.e. SETUP, OUT, IN
    unsigned int data_pid;              // 4 Data packet.data_pid
    unsigned int buffer_addr;           // 5
    unsigned int word_length;           // 6
    unsigned int tail_length_bytes;     // 7
    unsigned int ep_addr;               // 8
} XUH_ep_info;

XUH_ep_info xuh_ep_info[XUH_MAX_EPS * 2];

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
#define USB_PIDn_DATA0                  0xc3

unsigned Token_Setup[XUH_MAX_EPS];
unsigned Token_In[XUH_MAX_EPS];
unsigned Token_Out[XUH_MAX_EPS];

/* CRC[23:19], EP[18:15], Addr[14:8], PID[7:0] */
void GenerateTokens()
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

#ifdef __XS2A__
// from lib_xud/src/core/XUD_USBTile_Support.xc
int write_periph_word(tileref tile, unsigned peripheral, unsigned addr, unsigned data);
#endif


# if 0
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
#endif

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

// TODO Currently using port declarations from lib_xud
extern in buffered port:32  p_usb_clk;
extern in  port flag0_port;
extern in  port flag1_port;
#ifdef __XS2A__
extern in  port flag2_port;
#else
#define flag2_port null
#endif
extern out buffered port:32 p_usb_txd;
extern out port tx_readyout;
extern in port tx_readyin;

extern in buffered port:32 p_usb_rxd;
extern in port rx_rdy;

extern clock tx_usb_clk;
extern clock rx_usb_clk;
#endif

/* Wait for device to be detected and return speed */
#define STATE_IDLE 0
#define STATE_VP   1
#define STATE_VM   2

// 100ms debounce
#define CONNECT_DEBOUNCE (100000 * 100)

void XUD_HAL_EnableUsb(unsigned pwrConfig);
void XUD_HAL_Mode_Signalling(void);
void XUD_HAL_Mode_DataTransfer(void);

#ifndef __XS2A__
unsigned XtlSelFromMhz(unsigned m);

static unsigned xuh_xs3_usb_phy_cfg(unsigned xcvr_select, unsigned term_select,
                                    unsigned opmode, unsigned dp_pulldown,
                                    unsigned dm_pulldown)
{
    unsigned d = 0;
    d = XS1_USB_PHY_CFG0_UTMI_XCVRSELECT_SET(d, xcvr_select);
    d = XS1_USB_PHY_CFG0_UTMI_TERMSELECT_SET(d, term_select);
    d = XS1_USB_PHY_CFG0_UTMI_OPMODE_SET(d, opmode);
    d = XS1_USB_PHY_CFG0_DPPULLDOWN_SET(d, dp_pulldown);
    d = XS1_USB_PHY_CFG0_DMPULLDOWN_SET(d, dm_pulldown);
    d = XS1_USB_PHY_CFG0_UTMI_SUSPENDM_SET(d, 1);
    d = XS1_USB_PHY_CFG0_TXBITSTUFF_EN_SET(d, 1);
    d = XS1_USB_PHY_CFG0_PLL_EN_SET(d, 1);
    d = XS1_USB_PHY_CFG0_LPM_ALIVE_SET(d, 0);
    d = XS1_USB_PHY_CFG0_IDPAD_EN_SET(d, 0);
    d = XS1_USB_PHY_CFG0_XTLSEL_SET(d, XtlSelFromMhz(XUD_OSC_MHZ));
    return d;
}

static void xuh_xs3_write_usb_phy_cfg(unsigned d)
{
    write_sswitch_reg(get_local_tile_id(), XS1_SSWITCH_USB_PHY_CFG0_NUM, d);
}
#endif


#ifdef __XS2A__
extern tileref usb_tile;
#define xs1_su usb_tile
#define USB_TILE_REF usb_tile
#endif
void XUH_Manager(chanend c_ep_out[], unsigned epChanCount_out,
                 chanend c_ep_in[], unsigned epChanCount_in)
{
    timer t;
#define sof_timer t
  int state = STATE_IDLE;
    int nextState;
    unsigned time;
#if defined(__XS2A__) || defined(ARCH_S)
    unsigned int settings[1];
#endif
    int retVal = -1;

    GenerateTokens();

    for(unsigned i = 0; i < epChanCount_out; i++)
    {
        int x;
        /* Get resource ID of channel */
        asm("mov %0, %1" : "=r"(xuh_epChans0[i]) : "r"(c_ep_out[i]));

        asm("ldaw %0, %1[%2]":"=r"(x):"r"(xuh_epChans),"r"(i));
        xuh_ep_info[i].chan_array_ptr = x;

        asm("mov %0, %1":"=r"(x):"r"(c_ep_out[i]));
        xuh_ep_info[i].ep_xud_chanend = x;

        asm("getd %0, res[%1]":"=r"(x):"r"(c_ep_out[i]));
        xuh_ep_info[i].ep_client_chanend = x;

        /* Load memmory address */
        asm("ldaw %0, %1[%2]":"=r"(x):"r"(xuh_ep_info),"r"(i*sizeof(XUH_ep_info)/sizeof(unsigned)));

        xuh_ep_info[i].data_pid = USB_PIDn_DATA0;

        xuh_ep_info[i].token = Token_Setup[i];

        xuh_ep_info[i].ep_addr = i;

        /* Send memory address of EP struct over channel */
        outuint(c_ep_out[i], x);
    }

    for(unsigned i = 0; i < epChanCount_in; i++)
    {
        int x;
        /* Get resource ID of channel */
        asm("mov %0, %1" : "=r"(xuh_epChans0[i+XUH_MAX_EPS]) : "r"(c_ep_in[i]));

        asm("ldaw %0, %1[%2]":"=r"(x):"r"(xuh_epChans),"r"(i+XUH_MAX_EPS));
        xuh_ep_info[i+XUH_MAX_EPS].chan_array_ptr = x;

        asm("mov %0, %1":"=r"(x):"r"(c_ep_in[i]));
        xuh_ep_info[XUH_MAX_EPS+i].ep_xud_chanend = x;

        asm("getd %0, res[%1]":"=r"(x):"r"(c_ep_in[i]));
        xuh_ep_info[XUH_MAX_EPS+i].ep_client_chanend = x;

        xuh_ep_info[i+XUH_MAX_EPS].data_pid = USB_PIDn_DATA0;

        xuh_ep_info[i+XUH_MAX_EPS].ep_addr = i;

        /* Load memmory address */
        asm("ldaw %0, %1[%2]":"=r"(x):"r"(xuh_ep_info),"r"((i+XUH_MAX_EPS)*sizeof(XUH_ep_info)/sizeof(unsigned)));

        /* Send memory address of EP struct over channel */
        outuint(c_ep_in[i], x);
    }


    /* Make sure ports are on and reset port states */
    set_port_use_on(p_usb_clk);
#ifndef ARCH_S
    //set_port_clock(p_usb_clk, clk);
#endif
    set_port_use_on(p_usb_txd);
    set_port_use_on(p_usb_rxd);
    set_port_use_on(flag0_port);
    set_port_use_on(flag1_port);
#ifdef __XS2A__
    set_port_use_on(flag2_port);
#endif
#ifndef ARCH_S
    //set_port_use_on(reg_read_port);
    //set_port_use_on(reg_write_port);
#endif

#ifdef __XS2A__
    #define RX_RISE_DELAY 1
    #define RX_FALL_DELAY 5
    #define TX_RISE_DELAY 5
    #define TX_FALL_DELAY 1
    #define RX_ACTIVE_PAD_DELAY 2
#else
    #if (XUD_CORE_CLOCK >= 800)
        #define RX_RISE_DELAY 6
        #define TX_RISE_DELAY 3
        #define TX_FALL_DELAY 6
    #elif (XUD_CORE_CLOCK >= 700)
        #define RX_RISE_DELAY 5
        #define TX_RISE_DELAY 3
        #define TX_FALL_DELAY 5
    #elif (XUD_CORE_CLOCK >= 600)
        #define RX_RISE_DELAY 5
        #define TX_RISE_DELAY 3
        #define TX_FALL_DELAY 4
    #elif (XUD_CORE_CLOCK >= 500)
        #define RX_RISE_DELAY 4
        #define TX_RISE_DELAY 2
        #define TX_FALL_DELAY 2
    #elif (XUD_CORE_CLOCK >= 400)
        #define RX_RISE_DELAY 3
        #define TX_RISE_DELAY 2
        #define TX_FALL_DELAY 2
    #else
        #error XUD_CORE_CLOCK must be >= 400
    #endif
#endif

    configure_clock_src(tx_usb_clk, p_usb_clk);
    configure_clock_src(rx_usb_clk, p_usb_clk);

#ifdef __XS2A__
    // This, along with the following delays, forces the clock
    // to the ports to be effectively controlled by the
    // previous usb clock edges
    set_port_inv(p_usb_clk);

    // This delay controls the capture of rdy
    set_clock_rise_delay(tx_usb_clk, TX_RISE_DELAY);

    // This delay controls the launch of data.
    set_clock_fall_delay(tx_usb_clk, TX_FALL_DELAY);

    // This delay the capture of the rdyIn and data.
    set_clock_rise_delay(rx_usb_clk, RX_RISE_DELAY);
    set_clock_fall_delay(rx_usb_clk, RX_FALL_DELAY);

    set_pad_delay(flag1_port, RX_ACTIVE_PAD_DELAY);
#else
    /* XS3 uses the non-inverted USB clock and timing values from lib_xud. */
    set_clock_rise_delay(tx_usb_clk, TX_RISE_DELAY);
    set_clock_fall_delay(tx_usb_clk, TX_FALL_DELAY);
    set_clock_rise_delay(rx_usb_clk, RX_RISE_DELAY);
#endif
  	start_clock(tx_usb_clk);
  	start_clock(rx_usb_clk);

    configure_out_port_handshake(p_usb_txd, tx_readyin, tx_readyout, tx_usb_clk, 0);
  	configure_in_port_strobed_slave(p_usb_rxd, rx_rdy, rx_usb_clk);

    //printstr("Enable USB...");
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
    XUD_HAL_EnableUsb(XUD_PWR_BUS);

#endif
    /* Wait for USB clock (typically 1ms after reset) */
    p_usb_clk when pinseq(1) :> int _;
    p_usb_clk when pinseq(0) :> int _;
    p_usb_clk when pinseq(1) :> int _;
    p_usb_clk when pinseq(0) :> int _;
    t :> time;
    //printstr("USB clock ok\n");

    t :> time;
    t when timerafter(time+10000) :> void; // 40 uS

#ifdef __XS2A__
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
#else
    XUD_HAL_Mode_Signalling();
    xuh_xs3_write_usb_phy_cfg(xuh_xs3_usb_phy_cfg(1, 1, 0, 1, 1));
#endif

#ifdef __XS2A__
    // Write flag masks to UIFM
    write_periph_word(USB_TILE_REF, XS1_SU_PER_UIFM_CHANEND_NUM, XS1_SU_PER_UIFM_MASK_NUM,
            ((1<<XS1_SU_UIFM_IFM_FLAGS_K_SHIFT)
             | ((1<<XS1_SU_UIFM_IFM_FLAGS_J_SHIFT)<<8)
             | ((1<<XS1_SU_UIFM_IFM_FLAGS_SE0_SHIFT)<<16)));
#endif


    printstr("XUH waiting for device...\n");

    // Check DP/DM states for a device
    while (retVal < 0)
    {
      switch (state)
      {
        case STATE_IDLE:
            //printstr("IDLE: ");
            select
            {
                case flag0_port when pinsneq(0) :> void:
              //      printstr("STATE_VP\n");
                    nextState = STATE_VP;
                    break;
                case flag1_port when pinsneq(0) :> void:
                    nextState = STATE_VM;
                //    printstr("STATE_VM\n");
                    break;
            }
            break;
        case STATE_VP: // Full Speed
            sof_timer :> time;
            time += CONNECT_DEBOUNCE;
           // printstr("STATE_VP: ");
            select
            {
                case flag0_port when pinsneq(1) :> void:
                    nextState = STATE_IDLE;
             //       printstr("STATE_IDLE\n");
                    break;
                case flag1_port when pinsneq(0) :> void:
                    nextState = STATE_IDLE;
               //     printstr("STATE_IDLE\n");
                    break;
                case sof_timer when timerafter(time) :> void:
                 //   printstr("EXIT\n");
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

    if(retVal)
        printstr("LOW speed dev detected!\n");
    else
        printstr("FULL speed dev detected!\n");

    t :> time;
    t when timerafter(time+5000) :> void; // 40 uS

    // Drive SE0 on the bus (D+ and D- connected to ground via 45ohm resistors)
    // Set opmode to 0b10 for connrect chirp transmit and receive
    // OpMode: 0b10, TermSelect and XcvrSelect 0
#ifdef __XS2A__
    settings[0] = XS1_SU_UIFM_FUNC_CONTROL_OPMODE_SET(0, 0x2);
    settings[0] = XS1_SU_UIFM_FUNC_CONTROL_TERMSELECT_SET(settings[0], 0);
    settings[0] = XS1_SU_UIFM_FUNC_CONTROL_XCVRSELECT_SET(settings[0], 0);
    write_periph_32(xs1_su, XS1_SU_PER_UIFM_CHANEND_NUM, XS1_SU_PER_UIFM_FUNC_CONTROL_NUM, 1, settings);
#else
    xuh_xs3_write_usb_phy_cfg(xuh_xs3_usb_phy_cfg(0, 0, 0x2, 0, 0));
#endif

    /* Wait for chirp K from device to signal high-speed */
    flag1_port when pinseq(1) :> void;

    /* Wait for end of K chirp from device.. */
    flag1_port when pinseq(0) :> void;

    t :> time;
    t when timerafter(time+5000) :> void; // 40 uS

    clearbuf(p_usb_txd);

    /* Chirp back to device */
#define HOST_CHIRP_LENGTH 700
    for(int i = 0; i< 30; i++)
    {
        for(int j = 0; j < HOST_CHIRP_LENGTH; j++)
            p_usb_txd <: 0x0;

        for(int j = 0; j < HOST_CHIRP_LENGTH; j++)
            p_usb_txd <: 0xffffffff;
    }

    t :> time;
    t when timerafter(time+5000) :> void; // 40 uS

    /* Go into HS operation */
#ifdef __XS2A__
    settings[0] = XS1_SU_UIFM_FUNC_CONTROL_OPMODE_SET(0, 0x0); // OPMODE_0
    settings[0] = XS1_SU_UIFM_FUNC_CONTROL_TERMSELECT_SET(settings[0], 0);
    settings[0] = XS1_SU_UIFM_FUNC_CONTROL_XCVRSELECT_SET(settings[0], 0);
    write_periph_32(xs1_su, XS1_SU_PER_UIFM_CHANEND_NUM, XS1_SU_PER_UIFM_FUNC_CONTROL_NUM, 1, settings);
#else
    xuh_xs3_write_usb_phy_cfg(xuh_xs3_usb_phy_cfg(0, 0, 0x0, 0, 0));
#endif

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

    // TODO use XUD_HAL_Mode_DataTransfer()
    /* Setup flag ports for the loop.. */
#ifdef ARCH_S
  write_periph_word(USB_TILE_REF, XS1_GLX_PERIPH_USB_ID, XS1_UIFM_FLAGS_MASK_REG,
                ((1<<XS1_UIFM_IFM_FLAGS_NEWTOKEN)
                | ((1<<XS1_UIFM_IFM_FLAGS_RXACTIVE)<<8)
                | ((1<<XS1_UIFM_IFM_FLAGS_RXERROR)<<16)));
#else
    XUD_HAL_Mode_DataTransfer();
#endif

    XUH_IoLoop(xuh_epChans);
}


/*int main()
{
    printstr("Host test\n");

    hosttest();

}*/
