#include "main_globals.hpp"
#include "esp_log.h"
#include <atomic>

// used to force starting the stream setup process via commands
extern void force_activate_streaming();

static std::atomic<bool> s_startupCommandReceived{false};
bool getStartupCommandReceived()
{
    return s_startupCommandReceived.load();
}

void setStartupCommandReceived(bool startupCommandReceived)
{
    s_startupCommandReceived.store(startupCommandReceived);
}

// Global pause state
static std::atomic<bool> s_startupPaused{false};
bool getStartupPaused()
{
    return s_startupPaused.load();
}

void setStartupPaused(bool startupPaused)
{
    s_startupPaused.store(startupPaused);
}

// Function to manually activate streaming
void activateStreaming(void* arg)
{
    force_activate_streaming();
}

// USB handover state
static std::atomic<bool> s_usbHandoverDone{false};
bool getUsbHandoverDone()
{
    return s_usbHandoverDone.load();
}
void setUsbHandoverDone(bool done)
{
    s_usbHandoverDone.store(done);
}