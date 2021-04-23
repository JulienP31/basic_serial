#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/io.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>


// ---------- struct jp_uart_port ----------
struct jp_uart_port {
	struct uart_port port;
};


// ---------- struct jp_uart ----------
static struct uart_driver jp_uart = {
	.owner = THIS_MODULE,
	.driver_name = "jp_uart",
	.dev_name = "ttyJP",
	.major = TTY_MAJOR,
	.minor = 100,
	.nr = 2,
	.cons = NULL, /* NOTA : no console for this exercise */
};


// ---------- basic func() ----------
static unsigned int reg_read(struct uart_port *port, int off)
{
	return readl(port->membase + off * 4);
}


static void reg_write(struct uart_port *port, int off, int val)
{
	writel(val, port->membase + off * 4);
}


static void send_char(struct uart_port *port, unsigned char byte)
{
	// Wait for transmit register empty
	while ( !( reg_read(port, UART_LSR) & UART_LSR_THRE ) )
		cpu_relax();
	
	// Write char to transmit buffer
	reg_write(port, UART_TX, (int)byte);
}


// ---------- serial_read_irq() ----------
static irqreturn_t serial_read_irq(int irq, void *dev_id)
{
	struct uart_port *port = NULL;
	char status = 0;
	char ch = 0;
	unsigned int flag = TTY_NORMAL;
	int max_count = 256;
	
	port = (struct uart_port *)dev_id;
	
	do {
		// Read status & char
		status = reg_read(port, UART_LSR);
		ch = reg_read(port, UART_RX);
		
		// Update counters
		port->icount.rx++;
		
		if (status & UART_LSR_BI) {
			port->icount.brk++;
			if ( uart_handle_break(port) )
				continue;
		}
		else if (status & UART_LSR_PE)
			port->icount.parity++;
		else if (status & UART_LSR_FE)
			port->icount.frame++;
		else if (status & UART_LSR_OE)
			port->icount.overrun++;
		
		// Update flag
		status &= port->read_status_mask;
		
		if (status & UART_LSR_BI)
			flag = TTY_BREAK;
		else if (status & UART_LSR_PE)
			flag = TTY_PARITY;
		else if (status & UART_LSR_FE)
			flag = TTY_FRAME;
		
		if ( uart_handle_sysrq_char(port, ch) )
			continue;
		
		// Push char to UART layer
		uart_insert_char(port, status, UART_LSR_OE, ch, flag);
		pr_debug("uart_insert_char (char = %c - status = %x - flag = %d)", ch, status, flag);
	} while ( (max_count-- > 0) && (status & (UART_LSR_DR | UART_LSR_BI)) );
	
	// Push data to TTY layer
	tty_flip_buffer_push(&port->state->port);

	return IRQ_HANDLED;
}


// ---------- pops() ----------
static const char *jp_type(struct uart_port *port)
{
	pr_debug("Called %s\n", __func__);
	
	return (port->type == PORT_OMAP) ? "OMAP_SERIAL" : NULL;
}


static u_int jp_tx_empty(struct uart_port *port)
{
	pr_debug("Called %s\n", __func__);
	
	return TIOCSER_TEMT;
}


static void jp_set_mctrl(struct uart_port *port, u_int mctrl)
{
	pr_debug("Called %s\n", __func__);
}


static u_int jp_get_mctrl(struct uart_port *port)
{
	pr_debug("Called %s\n", __func__);
	
	return 0;
}


static void jp_start_tx(struct uart_port *port)
{
	struct circ_buf *xmit = NULL;
	
	pr_debug("Called %s\n", __func__);
	
	// Polled-mode transmission
	xmit = &port->state->xmit;
	
	while ( !uart_circ_empty(xmit) ) {
		send_char(port, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
	}
}


static void jp_stop_tx(struct uart_port *port)
{
	pr_debug("Called %s\n", __func__);
}


static void jp_stop_rx(struct uart_port *port)
{
	pr_debug("Called %s\n", __func__);
}


static int jp_startup(struct uart_port *port)
{
	unsigned int uartclk = 0;
	unsigned int baud_divisor = 0;
	
	pr_debug("Called %s\n", __func__);
	
	// Power management initialization
	pm_runtime_enable(port->dev);
	pm_runtime_get_sync(port->dev);
	
	// Configure baud rate to 115200
	of_property_read_u32(port->dev->of_node, "clock-frequency", &uartclk);
	baud_divisor = uartclk / 16 / 115200;
	reg_write(port, UART_OMAP_MDR1, UART_OMAP_MDR1_DISABLE); //< disable UART
	reg_write(port, UART_LCR, 0x00); //< reset line control register (no parity, 1 stop bit)
	reg_write(port, UART_LCR, UART_LCR_DLAB); //< enable divisor latch access
	reg_write(port, UART_DLL, baud_divisor & 0xff); //< set divisor latch low
	reg_write(port, UART_DLM, (baud_divisor >> 8) & 0xff); //< set divisor latch high
	reg_write(port, UART_LCR, UART_LCR_WLEN8); //< data length = 8 bits
	
	// Software reset
	reg_write(port, UART_FCR, UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT); //< clear RCVR & XMIT FIFOs
	reg_write(port, UART_OMAP_MDR1, UART_OMAP_MDR1_16X_MODE); //< enable UART
	
	// Enable RX IRQ
	reg_write(port, UART_IER_RDI, UART_IER);
	
	return 0;
}


static void jp_shutdown(struct uart_port *port)
{
	pr_debug("Called %s\n", __func__);
	
	// Disable RX IRQ
	reg_write(port, UART_IER_RDI, 0x00);
	
	// Disable PM
	pm_runtime_disable(port->dev);
}


static int jp_request_port(struct uart_port *port)
{
	struct platform_device *pdev = NULL;
	struct resource *res = NULL;
	int size = 0;
	
	pr_debug("Called %s\n", __func__);
	
	// Request IO memory
	pdev = to_platform_device(port->dev->parent);
	size = resource_size(pdev->resource);
	
	res = devm_request_mem_region(port->dev, port->mapbase, size, "jp_serial");
	if (!res)
		return -EBUSY;
	
	// Remap IO memory in virtual memory
	if (port->flags & UPF_IOREMAP) {
		port->membase = devm_ioremap(port->dev, res->start, size);
		if (!port->membase) {
			release_mem_region(port->mapbase, size);
			return -ENOMEM;
		}
	}
		
	return 0;
}


static void jp_config_port(struct uart_port *port, int flags)
{
	pr_debug("Called %s\n", __func__);

	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_OMAP;
		jp_request_port(port);
	}
}


static void jp_release_port(struct uart_port *port)
{
	pr_debug("Called %s\n", __func__);
}


static void jp_set_termios(struct uart_port *port, struct ktermios *termios, struct ktermios *old)
{
	pr_debug("Called %s\n", __func__);
}


// ---------- struct jp_pops ----------
static const struct uart_ops jp_pops = {
	.type		= jp_type,
	.tx_empty	= jp_tx_empty,
	.set_mctrl	= jp_set_mctrl,
	.get_mctrl	= jp_get_mctrl,
	.start_tx	= jp_start_tx,
	.stop_tx	= jp_stop_tx,
	.stop_rx	= jp_stop_rx,
	.startup	= jp_startup,
	.shutdown	= jp_shutdown,
	.request_port	= jp_request_port,
	.config_port	= jp_config_port,
	.release_port	= jp_release_port,
	.set_termios	= jp_set_termios,
};


// ---------- jp_serial_probe() ----------
static int jp_serial_probe(struct platform_device *pdev)
{
	struct jp_uart_port *jp_port = NULL;
	struct uart_port *port = NULL;
	struct resource *res = NULL;
	int ret = 0;
	
	pr_debug("Called %s\n", __func__);
	
	// Allocate memory
	jp_port = devm_kmalloc(&pdev->dev, sizeof(struct jp_uart_port), GFP_KERNEL);
	if (!jp_port)
		return -ENOMEM;
	
	// Initialize port infos
	port = &jp_port->port;
	
	port->iotype = UPIO_MEM;
	
	port->flags = UPF_BOOT_AUTOCONF | UPF_IOREMAP; //< NOTA : make jp_config_port() & jp_request_port() called !
	
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOMEM;
	port->mapbase = res->start; //< physical address
	
	port->membase = NULL; //< virtual address -> set in jp_request_port()
	
	port->uartclk = 0; //< set in jp_startup()
	
	port->ops = &jp_pops;
	
	port->dev = &pdev->dev; //< NOTA : useful in pops() !
	
	port->irq = platform_get_irq(pdev, 0);
	
	// Add port
	ret = uart_add_one_port(&jp_uart, port);
	if (ret)
		return ret;
	
	// Register IRQ
	ret = devm_request_irq(port->dev, port->irq, serial_read_irq, 0, "serial_read_irq", port);
	if (ret < 0)
		return ret;
	
	platform_set_drvdata(pdev, jp_port); //< NOTA : useful in jp_serial_remove() !
	
	return ret;
}


// ---------- jp_serial_remove() ----------
static int jp_serial_remove(struct platform_device *pdev)
{
	struct jp_uart_port *jp_port = NULL;
	int ret = 0;
	
	pr_debug("Called %s\n", __func__);
	
	jp_port = platform_get_drvdata(pdev);
	platform_set_drvdata(pdev, NULL);
	
	// Remove port
	ret = uart_remove_one_port(&jp_uart, &jp_port->port);
	
        return ret;
}


// ---------- serial_of_match[] ----------
static const struct of_device_id serial_of_match[] = {
	{ .compatible = "jp,serial" },
	{ },
};
MODULE_DEVICE_TABLE(of, serial_of_match);


// ---------- struct jp_serial_driver ----------
static struct platform_driver jp_serial_driver = {
	.probe = jp_serial_probe,
	.remove = jp_serial_remove,
	.driver = {
		.name = "jp_serial",
		.owner = THIS_MODULE,
		.of_match_table = serial_of_match,
	},
};


// ---------- jp_serial_init() ----------
static int __init jp_serial_init(void)
{
	int ret = 0;
	
	pr_debug("Called %s\n", __func__);
	
	// Register driver
	ret = uart_register_driver(&jp_uart);
	if (ret)
		return ret;
	
	ret = platform_driver_register(&jp_serial_driver);
	if (ret)
		uart_unregister_driver(&jp_uart);
	
	return ret;
}


// ---------- jp_serial_exit() ----------
static void __exit jp_serial_exit(void)
{
	pr_debug("Called %s\n", __func__);

	// Unregister driver
	platform_driver_unregister(&jp_serial_driver);
	uart_unregister_driver(&jp_uart);
}


module_init(jp_serial_init);
module_exit(jp_serial_exit);


MODULE_DESCRIPTION("Basic serial driver exercise");
MODULE_AUTHOR("Julien Panis <julienpanis@hotmail.com>");
MODULE_LICENSE("GPL v2");

