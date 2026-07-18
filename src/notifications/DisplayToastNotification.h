#pragma once

#include <Poco/Notification.h>

/**
 * @brief Informs the GUI subsystem to queue a new toast message.
 */
class DisplayToastNotification : public Poco::Notification
{
public:
    std::string name() const override;

    DisplayToastNotification() = delete;

    explicit DisplayToastNotification(std::string toastText, float displayTime = 3.0f);

    const std::string& ToastText() const;

    float DisplayTime() const;

private:
    std::string _toastText;
    float _displayTime;
};
