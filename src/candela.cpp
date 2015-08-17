#include <array>
#include <algorithm>
#include <cstring>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <thread>

#include <xcb/xcb.h>
#include <xcb/xcb_util.h>
#include <xcb/xproto.h>
#include <xcb/randr.h>

namespace candella
{
struct version_info
{
    int major = 0;
    int minor = 0;
    int patch = 1;
} version;

inline std::ostream& operator<<(std::ostream& os, const version_info& ver)
{
    return os << ver.major << "." << ver.minor << "." << ver.patch;
}

struct configuration
{
    /// the poll time
    const std::chrono::milliseconds poll_time{500};

    /// the fade effect duration
    const std::chrono::milliseconds fade_time{200};

    /// the number of steps to run during the fade
    const std::size_t fade_steps = 10;

    /// the file to read the ambient light values from
    const std::string light_sensor = "/sys/devices/platform/applesmc.768/light";

    /// the maximum value for the light sensor output
    const int max_light = 255;

    /// the minimum desired brightness value
    const int min_bright = 7;

    /// a function to convert the light sensor reading to a percentage
    static double ambient_reading(const std::string& reading)
    {
        assert(reading.size() > 1);
        auto comma = reading.find(',');
        auto end = comma == std::string::npos ? reading.size() : comma;
        auto val = reading.substr(1, end - 1);
        return std::stoi(val) / 255.0;
    }

    /// a function to scale the brightness based on the ambient light
    int desired_brightness(int max_brightness, double ambient_reading)
    {
        return max_brightness * std::log(1 + 100 * ambient_reading)
               / std::log(101);
    }
} config;

class ambient_light
{
  public:
    ambient_light(const std::string& light_sensor) : sensor_{light_sensor}
    {
        std::fill(values_.begin(), values_.end(), read_value());
    }

    class exception : public std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    double poll()
    {
        std::copy(values_.begin() + 1, values_.end(), values_.begin());
        values_.back() = read_value();

        return std::accumulate(values_.begin(), values_.end(), 0.0)
               / values_.size();
    }

  private:
    double read_value()
    {
        sensor_.seekg(0, std::ios::beg);
        std::string line;
        if (!std::getline(sensor_, line))
            throw exception{"failed to read light sensor value"};

        return config.ambient_reading(line);
    }

    std::ifstream sensor_;
    std::array<double, 10> values_;
};

enum class action_type
{
    BACKLIGHT_POLL,
    BACKLIGHT_FADE
};

class backlight_adjuster
{
  public:
    backlight_adjuster()
    {
        xcb_generic_error_t* error;
        // most of the below is lifted from xbacklight
        conn_ = ::xcb_connect(nullptr, nullptr);
        auto backlight_cookie
            = ::xcb_intern_atom(conn_, 1, strlen("Backlight"), "Backlight");
        auto backlight_reply
            = ::xcb_intern_atom_reply(conn_, backlight_cookie, &error);

        if (error || !backlight_reply)
            throw exception{
                "Failed to obtain minimum and maximum brightness values"};

        backlight_ = backlight_reply->atom;
        free(backlight_reply);

        if (backlight_ == XCB_NONE)
            throw exception{"Couldn't find an output with backlight property"};

        auto root
            = ::xcb_setup_roots_iterator(::xcb_get_setup(conn_)).data->root;
        auto resources_cookie = ::xcb_randr_get_screen_resources(conn_, root);
        auto resources_reply = ::xcb_randr_get_screen_resources_reply(
            conn_, resources_cookie, &error);
        if (error || resources_reply == nullptr)
            throw exception{"Couldn't get screen resources"};

        output_ = ::xcb_randr_get_screen_resources_outputs(resources_reply)[0];
        free(resources_reply);

        auto prop_cookie
            = ::xcb_randr_query_output_property(conn_, output_, backlight_);
        auto prop_reply = ::xcb_randr_query_output_property_reply(
            conn_, prop_cookie, &error);

        if (error || prop_reply == nullptr || !prop_reply->range
            || ::xcb_randr_query_output_property_valid_values_length(prop_reply)
                   != 2)
            throw exception{"Couldn't query min/max brightness values"};

        auto values
            = ::xcb_randr_query_output_property_valid_values(prop_reply);
        min_bright_ = values[0];
        max_bright_ = values[1];
        free(prop_reply);
    }

    int32_t current_brightness() const
    {
        xcb_generic_error_t* error;
        auto prop_cookie = ::xcb_randr_get_output_property(
            conn_, output_, backlight_, XCB_ATOM_NONE, 0, 4, 0, 0);
        auto prop_reply
            = ::xcb_randr_get_output_property_reply(conn_, prop_cookie, &error);
        if (error || !prop_reply || prop_reply->type != XCB_ATOM_INTEGER
            || prop_reply->num_items != 1 || prop_reply->format != 32)
            throw exception{"Failed to query current brightness"};

        auto curr_bright
            = *reinterpret_cast<int32_t*>(
                  ::xcb_randr_get_output_property_data(prop_reply));
        free(prop_reply);
        return curr_bright;
    }

    void set_brightness(int32_t value)
    {
        ::xcb_randr_change_output_property(
            conn_, output_, backlight_, XCB_ATOM_INTEGER, 32,
            XCB_PROP_MODE_REPLACE, 1, reinterpret_cast<uint8_t*>(&value));
        ::xcb_flush(conn_);
    }

    int32_t min_brighness() const
    {
        return min_bright_;
    }

    int32_t max_brightness() const
    {
        return max_bright_;
    }

    ~backlight_adjuster()
    {
        ::xcb_disconnect(conn_);
    }

    class exception : public std::runtime_error
    {
      public:
        using std::runtime_error::runtime_error;
    };

  private:
    xcb_connection_t* conn_;
    xcb_atom_t backlight_;
    xcb_randr_output_t output_;
    int32_t min_bright_;
    int32_t max_bright_;
};

void loop()
{
    backlight_adjuster adjuster;
    ambient_light sensor{config.light_sensor};

    using steady_clock = std::chrono::system_clock;
    std::multimap<steady_clock::time_point, action_type> timer_queue;
    timer_queue.emplace(steady_clock::now(), action_type::BACKLIGHT_POLL);

    int32_t curr_bright = 0;
    int32_t new_bright = 0;
    int32_t step_size = 0;

    while (true)
    {
        std::this_thread::sleep_until(timer_queue.begin()->first);
        if (timer_queue.begin()->second == action_type::BACKLIGHT_POLL)
        {
            // read the current ambient light value
            auto ambient_light = sensor.poll();
            std::cerr << "[info]: Ambient light value was " << ambient_light
                      << "\n";

            // read the current backlight value
            curr_bright = adjuster.current_brightness();
            std::cerr << "[info]: current brightness " << curr_bright << "\n";

            // calculate the new brightness value
            new_bright = config.desired_brightness(adjuster.max_brightness(),
                                                   ambient_light);

            new_bright = std::max(new_bright, config.min_bright);

            if (curr_bright != new_bright)
            {
                std::cerr << "[info]: Adjusting brightness to " << new_bright
                          << "\n";
                step_size = (new_bright - curr_bright) / config.fade_steps;
                for (std::size_t i = 0; i < config.fade_steps; ++i)
                {
                    timer_queue.emplace(steady_clock::now()
                                            + i * config.fade_time
                                                  / config.fade_steps,
                                        action_type::BACKLIGHT_FADE);
                }
            }
            timer_queue.emplace(steady_clock::now() + config.poll_time,
                                action_type::BACKLIGHT_POLL);
        }
        else if (timer_queue.begin()->second == action_type::BACKLIGHT_FADE)
        {
            curr_bright += step_size;
            if (step_size == 0 || curr_bright > new_bright)
                curr_bright = new_bright;

            adjuster.set_brightness(curr_bright);
            if (curr_bright != new_bright)
            {
                timer_queue.emplace(steady_clock::now()
                                        + config.fade_time / config.fade_steps,
                                    action_type::BACKLIGHT_FADE);
            }
        }
        timer_queue.erase(timer_queue.begin());
    }
}
}

int main()
{
    std::cerr << "candela " << candella::version << " starting...\n";

    try
    {
        candella::loop();
        return 0;
    }
    catch (candella::backlight_adjuster::exception& ex)
    {
        std::cerr << "[fatal]: " << ex.what() << "\n";
        return 1;
    }
}
