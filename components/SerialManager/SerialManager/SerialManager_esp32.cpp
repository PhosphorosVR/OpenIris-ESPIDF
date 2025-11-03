#include "SerialManager.hpp"
#include "esp_log.h"
#include "main_globals.hpp"
#include "driver/uart.h"

void SerialManager::setup()
{
  uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
  };

  const auto uart_num = static_cast<uart_port_t>(CONFIG_UART_PORT_NUMBER);

  uart_driver_install(uart_num, BUF_SIZE, BUF_SIZE, 0, NULL, 0);
  uart_param_config(uart_num, &uart_config);

  uart_set_pin(uart_num,
               CONFIG_UART_TX_PIN,
               CONFIG_UART_RX_PIN,
               UART_PIN_NO_CHANGE,
               UART_PIN_NO_CHANGE);
}

void uart_write_bytes_chunked(uart_port_t uart_num, const void *src, size_t size)
{
  while (size > 0)
  {
    auto to_write = size > BUF_SIZE ? BUF_SIZE : size;
    auto written = uart_write_bytes(uart_num, src, to_write);
    src += written;
    size -= written;
  }
}

void SerialManager::try_receive()
{
  static auto current_position = 0;
  const auto uart_num = static_cast<uart_port_t>(CONFIG_UART_PORT_NUMBER);
  int len = uart_read_bytes(uart_num, this->temp_data, BUF_SIZE, 1000 / 20);

  // If driver is uninstalled or an error occurs, abort read gracefully
  if (len <= 0)
  {
    return;
  }

  if (len > 0)
  {
    notify_startup_command_received();
  }

  // since we've got something on the serial port
  // we gotta keep reading until we've got the whole message
  // we will submit the command once we get a newline, a return or the buffer is full
  for (auto i = 0; i < len; i++)
  {
    this->data[current_position++] = this->temp_data[i];
    // if we're at the end of the buffer, try to process the command anyway
    // if we've got a new line, we've finished sending the commands, process them
    if (current_position >= BUF_SIZE || this->data[current_position - 1] == '\n' || this->data[current_position - 1] == '\r')
    {
      data[current_position] = '\0';
      current_position = 0;

      const nlohmann::json result = this->commandManager->executeFromJson(std::string_view(reinterpret_cast<const char *>(this->data)));
      const auto resultMessage = result.dump();
      // todo check if this works
      // uart_write_bytes_chunked(uart_num, resultMessage.c_str(), resultMessage.length())s
      uart_write_bytes(uart_num, resultMessage.c_str(), resultMessage.length());
    }
  }
}

void SerialManager::shutdown()
{
  // Uninstall the UART driver to free the internal to keep compatibility with JTAG implementation.
  const auto uart_num = static_cast<uart_port_t>(CONFIG_UART_PORT_NUMBER);
  esp_err_t err = uart_driver_delete(uart_num);
  if (err == ESP_OK)
  {
    ESP_LOGI("[SERIAL]", "usb_serial_jtag driver uninstalled");
  }
  else if (err != ESP_ERR_INVALID_STATE)
  {
    ESP_LOGW("[SERIAL]", "usb_serial_jtag_driver_uninstall returned %s", esp_err_to_name(err));
  }
}
