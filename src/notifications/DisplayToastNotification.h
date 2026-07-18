#pragma once

#include "notifications/ToastOptions.h"

#include <Poco/Notification.h>

#include <string>
#include <utility>

/**
 * @brief Informs the GUI subsystem to queue a new toast message.
 */
class DisplayToastNotification : public Poco::Notification
{
public:
    std::string name() const override;

    DisplayToastNotification() = delete;

    /// Convenience constructor for a plain, centered toast.
    explicit DisplayToastNotification(std::string toastText, float displayTime = 3.0f)
    {
        _options.text = std::move(toastText);
        _options.displayTime = displayTime;
    }

    explicit DisplayToastNotification(ToastOptions options)
        : _options(std::move(options))
    {
    }

    const ToastOptions& Options() const;

private:
    ToastOptions _options;
};
