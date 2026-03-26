#include "SerialManager.hpp"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_vfs_usb_serial_jtag.h"
#include "main_globals.hpp"
#include "soc/usb_serial_jtag_reg.h"

#include "tusb.h"

void SerialManager::setup()
{
#ifndef CONFIG_USE_UART_FOR_COMMUNICATION
    usb_serial_jtag_driver_config_t usb_serial_jtag_config;
    usb_serial_jtag_config.rx_buffer_size = BUF_SIZE;
    usb_serial_jtag_config.tx_buffer_size = BUF_SIZE;
    usb_serial_jtag_driver_install(&usb_serial_jtag_config);
#endif
}

void usb_serial_jtag_write_bytes_chunked(const char* data, size_t len, size_t timeout)
{
#ifndef CONFIG_USE_UART_FOR_COMMUNICATION
    while (len > 0)
    {
        auto to_write = len > BUF_SIZE ? BUF_SIZE : len;
        auto written = usb_serial_jtag_write_bytes(data, to_write, timeout);
        data += written;
        len -= written;
    }
#endif
}

void SerialManager::try_receive()
{
    static auto current_position = 0;
    int len = usb_serial_jtag_read_bytes(this->temp_data, 256, 1000 / 20);

    // If driver is uninstalled or an error occurs, abort read gracefully
    if (len < 0)
    {
        return;
    }

    if (len > 0)
    {
        // Notify main that a command was received during startup
        notify_startup_command_received();
    }

    // since we've got something on the serial port
    // we gotta keep reading until we've got the whole message
    // we will submit the command once we get a newline, a return or the buffer is full
    for (auto i = 0; i < len; i++)
    {
        const char c = this->temp_data[i];

        if (current_position >= BUF_SIZE - 1 || c == '\n' || c == '\r')
        {
            this->data[current_position] = '\0';
            if (current_position > 0)
            {
                const nlohmann::json result = this->commandManager->executeFromJson(std::string_view(reinterpret_cast<const char*>(this->data)));
                const auto resultMessage = result.dump() + "\n";
                usb_serial_jtag_write_bytes_chunked(resultMessage.c_str(), resultMessage.length(), 1000 / 20);
            }
            current_position = 0;
            continue;
        }

        this->data[current_position++] = c;
    }
}

void SerialManager::shutdown()
{
    // Uninstall the USB Serial JTAG driver to free the internal USB for TinyUSB.
    esp_err_t err = usb_serial_jtag_driver_uninstall();
    if (err == ESP_OK)
    {
        ESP_LOGI("[SERIAL]", "usb_serial_jtag driver uninstalled");
    }
    else if (err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW("[SERIAL]", "usb_serial_jtag_driver_uninstall returned %s", esp_err_to_name(err));
    }

    // Explicitly pull D+ low so the USB host detects a disconnect *now*.
    // usb_serial_jtag_driver_uninstall() frees driver resources but leaves the
    // hardware D+ pullup active.  Without this, D+ stays high through the entire
    // handover delay and into usb_new_phy(), which atomically switches D+ from
    // the JTAG peripheral to the OTG peripheral — giving the host only a
    // nanosecond-scale glitch.  That is too brief for Windows (and most hosts)
    // to tear down the JTAG device before the new UVC device appears, causing
    // enumeration to fail or the device to remain detected as JTAG.
    // After this call the 200 ms delay in startWiredMode() gives the host a
    // proper window to process the disconnect before OTG D+ goes high again.
    SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PAD_PULL_OVERRIDE);
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_DP_PULLUP);
}

void HandleCDCSerialManagerTask(void* pvParameters)
{
#ifndef CONFIG_USE_UART_FOR_COMMUNICATION
    auto const commandManager = static_cast<CommandManager*>(pvParameters);
    static char buffer[BUF_SIZE];
    auto idx = 0;

    cdc_command_packet_t packet;
    while (true)
    {
        if (xQueueReceive(cdcMessageQueue, &packet, portMAX_DELAY) == pdTRUE)
        {
            for (auto i = 0; i < packet.len; i++)
            {
                const char c = packet.data[i];

                if (idx >= BUF_SIZE - 1 || c == '\n' || c == '\r')
                {
                    buffer[idx] = '\0';
                    if (idx > 0)
                    {
                        const nlohmann::json result = commandManager->executeFromJson(std::string_view(reinterpret_cast<const char*>(buffer)));
                        const auto resultMessage = result.dump() + "\n";
                        tud_cdc_write(resultMessage.c_str(), resultMessage.length());
                        tud_cdc_write_flush();
                    }
                    idx = 0;
                    continue;
                }

                buffer[idx++] = c;
            }
        }
    }
#endif
}

// tud_cdc_rx_cb is defined as TU_ATTR_WEAK so we can override it, we will be called back if we get some data
// but we don't want to do any processing here since we don't want to risk blocking
// grab the data and send it to a queue, a special task will process it and handle with the command manager
extern "C" void tud_cdc_rx_cb(uint8_t itf)
{
    // we can void the interface number
    (void)itf;
    cdc_command_packet_t packet;
    auto len = tud_cdc_available();

    if (len > 0)
    {
        auto read = tud_cdc_read(packet.data, sizeof(packet.data));
        if (read > 0)
        {
            // we should be safe here, given that the max buffer size is 64
            packet.len = static_cast<uint8_t>(read);
            xQueueSend(cdcMessageQueue, &packet, 1);
        }
    }
}

extern "C" void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    (void)itf;
    (void)dtr;
    (void)rts;

    ESP_LOGI("[SERIAL]", "CDC line state changed: DTR=%d, RTS=%d", dtr, rts);
}

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* p_line_coding)
{
    (void)itf;
    ESP_LOGI("[SERIAL]", "CDC line coding: %" PRIu32 " bps, %d stop bits, %d parity, %d data bits", p_line_coding->bit_rate, p_line_coding->stop_bits,
             p_line_coding->parity, p_line_coding->data_bits);
}