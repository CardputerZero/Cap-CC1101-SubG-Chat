#include "gpio_line.h"

#include <cerrno>
#include <cstring>
#include <gpiod.h>
#include <stdexcept>
#include <string>

namespace {
std::string& chipPathStorage()
{
    static std::string chip_path = "/dev/gpiochip0";
    return chip_path;
}

std::string err(const std::string& action)
{
    return action + " failed: " + std::strerror(errno);
}
}  // namespace

GpioLine::~GpioLine()
{
    releaseLine();
    if (chip_) {
        gpiod_chip_close(chip_);
        chip_ = nullptr;
    }
}

void GpioLine::setDefaultChip(const std::string& chip_path)
{
    chipPathStorage() = chip_path;
}

const std::string& GpioLine::defaultChip()
{
    return chipPathStorage();
}

const std::string& GpioLine::chipPath() const
{
    return chip_path_.empty() ? chipPathStorage() : chip_path_;
}

void GpioLine::ensureLine() const
{
    if (!valid()) return;
    if (!chip_) {
        chip_ = gpiod_chip_open(chipPath().c_str());
        if (!chip_) throw std::runtime_error(err("gpiod_chip_open " + chipPath()));
    }
#ifndef GPIOD_V2
    if (!line_) {
        line_ = gpiod_chip_get_line(chip_, static_cast<unsigned int>(gpio_));
        if (!line_) throw std::runtime_error(err("gpiod_chip_get_line " + std::to_string(gpio_)));
    }
#endif
}

void GpioLine::releaseLine() const
{
#ifdef GPIOD_V2
    if (request_) {
        gpiod_line_request_release(request_);
        request_   = nullptr;
        requested_ = false;
    }
#else
    if (requested_ && line_) {
        gpiod_line_release(line_);
        requested_ = false;
    }
#endif
}

void GpioLine::requestInput() const
{
    ensureLine();
    if (!valid()) return;
    releaseLine();
#ifdef GPIOD_V2
    gpiod_line_settings* settings        = gpiod_line_settings_new();
    gpiod_line_config* line_config       = gpiod_line_config_new();
    gpiod_request_config* request_config = gpiod_request_config_new();
    if (!settings || !line_config || !request_config) {
        gpiod_line_settings_free(settings);
        gpiod_line_config_free(line_config);
        gpiod_request_config_free(request_config);
        throw std::runtime_error(err("gpiod request config allocation"));
    }
    unsigned int offset = static_cast<unsigned int>(gpio_);
    int rc              = gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    if (rc == 0) rc = gpiod_line_config_add_line_settings(line_config, &offset, 1, settings);
    gpiod_request_config_set_consumer(request_config, "cc1101-chat");
    if (rc == 0) request_ = gpiod_chip_request_lines(chip_, request_config, line_config);
    gpiod_line_settings_free(settings);
    gpiod_line_config_free(line_config);
    gpiod_request_config_free(request_config);
    if (rc < 0 || !request_) throw std::runtime_error(err("gpiod request input GPIO" + std::to_string(gpio_)));
#else
    if (gpiod_line_request_input(line_, "cc1101-chat") < 0) {
        throw std::runtime_error(err("gpiod_line_request_input GPIO" + std::to_string(gpio_)));
    }
#endif
    requested_ = true;
    direction_ = Direction::In;
}

void GpioLine::requestOutput(bool value) const
{
    ensureLine();
    if (!valid()) return;
    releaseLine();
#ifdef GPIOD_V2
    gpiod_line_settings* settings        = gpiod_line_settings_new();
    gpiod_line_config* line_config       = gpiod_line_config_new();
    gpiod_request_config* request_config = gpiod_request_config_new();
    if (!settings || !line_config || !request_config) {
        gpiod_line_settings_free(settings);
        gpiod_line_config_free(line_config);
        gpiod_request_config_free(request_config);
        throw std::runtime_error(err("gpiod request config allocation"));
    }
    unsigned int offset = static_cast<unsigned int>(gpio_);
    int rc              = gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    if (rc == 0) {
        rc =
            gpiod_line_settings_set_output_value(settings, value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
    }
    if (rc == 0) rc = gpiod_line_config_add_line_settings(line_config, &offset, 1, settings);
    gpiod_request_config_set_consumer(request_config, "cc1101-chat");
    if (rc == 0) request_ = gpiod_chip_request_lines(chip_, request_config, line_config);
    gpiod_line_settings_free(settings);
    gpiod_line_config_free(line_config);
    gpiod_request_config_free(request_config);
    if (rc < 0 || !request_) throw std::runtime_error(err("gpiod request output GPIO" + std::to_string(gpio_)));
#else
    if (gpiod_line_request_output(line_, "cc1101-chat", value ? 1 : 0) < 0) {
        throw std::runtime_error(err("gpiod_line_request_output GPIO" + std::to_string(gpio_)));
    }
#endif
    requested_    = true;
    direction_    = Direction::Out;
    output_value_ = value;
}

void GpioLine::requestRisingEdge() const
{
    ensureLine();
    if (!valid()) return;
    releaseLine();
#ifdef GPIOD_V2
    gpiod_line_settings* settings        = gpiod_line_settings_new();
    gpiod_line_config* line_config       = gpiod_line_config_new();
    gpiod_request_config* request_config = gpiod_request_config_new();
    if (!settings || !line_config || !request_config) {
        gpiod_line_settings_free(settings);
        gpiod_line_config_free(line_config);
        gpiod_request_config_free(request_config);
        throw std::runtime_error(err("gpiod request config allocation"));
    }
    unsigned int offset = static_cast<unsigned int>(gpio_);
    int rc              = gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    if (rc == 0) rc = gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_RISING);
    if (rc == 0) rc = gpiod_line_config_add_line_settings(line_config, &offset, 1, settings);
    gpiod_request_config_set_consumer(request_config, "cc1101-chat");
    if (rc == 0) request_ = gpiod_chip_request_lines(chip_, request_config, line_config);
    gpiod_line_settings_free(settings);
    gpiod_line_config_free(line_config);
    gpiod_request_config_free(request_config);
    if (rc < 0 || !request_) throw std::runtime_error(err("gpiod request rising edge GPIO" + std::to_string(gpio_)));
#else
    if (gpiod_line_request_rising_edge_events(line_, "cc1101-chat") < 0) {
        throw std::runtime_error(err("gpiod_line_request_rising_edge_events GPIO" + std::to_string(gpio_)));
    }
#endif
    requested_ = true;
    direction_ = Direction::In;
}

void GpioLine::exportLine()
{
    ensureLine();
}

void GpioLine::unexportLine()
{
    releaseLine();
}

void GpioLine::setDirection(Direction dir)
{
    if (!valid()) return;
    if (dir == Direction::Out)
        requestOutput(output_value_);
    else
        requestInput();
}

void GpioLine::setValue(bool value)
{
    if (!valid()) return;
    ensureLine();
    if (!requested_ || direction_ != Direction::Out) requestOutput(value);
#ifdef GPIOD_V2
    if (gpiod_line_request_set_value(request_, static_cast<unsigned int>(gpio_),
                                     value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE) < 0) {
        throw std::runtime_error(err("gpiod_line_request_set_value GPIO" + std::to_string(gpio_)));
    }
#else
    if (gpiod_line_set_value(line_, value ? 1 : 0) < 0) {
        throw std::runtime_error(err("gpiod_line_set_value GPIO" + std::to_string(gpio_)));
    }
#endif
    output_value_ = value;
}

bool GpioLine::getValue() const
{
    if (!valid()) return false;
    ensureLine();
    if (!requested_) requestInput();
#ifdef GPIOD_V2
    gpiod_line_value value = gpiod_line_request_get_value(request_, static_cast<unsigned int>(gpio_));
    if (value < 0) throw std::runtime_error(err("gpiod_line_request_get_value GPIO" + std::to_string(gpio_)));
    return value == GPIOD_LINE_VALUE_ACTIVE;
#else
    int value = gpiod_line_get_value(line_);
    if (value < 0) throw std::runtime_error(err("gpiod_line_get_value GPIO" + std::to_string(gpio_)));
    return value != 0;
#endif
}

void GpioLine::setEdge(const std::string& edge)
{
    if (!valid()) return;
    if (edge == "rising")
        requestRisingEdge();
    else if (edge == "none")
        requestInput();
    else
        throw std::runtime_error("libgpiod backend currently supports edge 'rising' or 'none'");
}

bool GpioLine::waitForEdge(int timeout_ms) const
{
    if (!valid()) return false;
    ensureLine();
    if (!requested_ || direction_ != Direction::In) requestRisingEdge();
#ifdef GPIOD_V2
    int64_t timeout_ns = -1;
    if (timeout_ms >= 0) timeout_ns = static_cast<int64_t>(timeout_ms) * 1000000LL;
    int rc = gpiod_line_request_wait_edge_events(request_, timeout_ns);
    if (rc < 0) throw std::runtime_error(err("gpiod_line_request_wait_edge_events GPIO" + std::to_string(gpio_)));
    if (rc == 0) return false;

    gpiod_edge_event_buffer* buffer = gpiod_edge_event_buffer_new(1);
    if (!buffer) throw std::runtime_error(err("gpiod_edge_event_buffer_new"));
    rc = gpiod_line_request_read_edge_events(request_, buffer, 1);
    gpiod_edge_event_buffer_free(buffer);
    if (rc < 0) throw std::runtime_error(err("gpiod_line_request_read_edge_events GPIO" + std::to_string(gpio_)));
    return rc > 0;
#else
    timespec ts{};
    timespec* tsp = nullptr;
    if (timeout_ms >= 0) {
        ts.tv_sec  = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
        tsp        = &ts;
    }
    int rc = gpiod_line_event_wait(line_, tsp);
    if (rc < 0) throw std::runtime_error(err("gpiod_line_event_wait GPIO" + std::to_string(gpio_)));
    if (rc == 0) return false;

    gpiod_line_event event{};
    if (gpiod_line_event_read(line_, &event) < 0) {
        throw std::runtime_error(err("gpiod_line_event_read GPIO" + std::to_string(gpio_)));
    }
    return true;
#endif
}

void GpioLine::setLine(int gpio, bool value)
{
    GpioLine line(gpio);
    line.exportLine();
    line.setDirection(Direction::Out);
    line.setValue(value);
}
