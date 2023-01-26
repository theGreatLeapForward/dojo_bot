//
// Created by thegreatleapforward on 11/01/23.
//

#pragma once

#include <spdlog/fmt/fmt.h>
#include <dpp/dpp.h>
#include <ranges>
#include <chrono>
#include <regex>
#include <unordered_map>
#include <vector>
#include <set>

using namespace std::chrono_literals;
namespace ran = std::ranges;
namespace chr = std::chrono;
using dpp::snowflake;
using dpp::message;
using id_vec = const std::vector<snowflake>;
using coc_vec = std::vector<dpp::command_option_choice>;

template<class Rep, class Period>
auto cur_msg_time(const std::chrono::duration<Rep, Period>& offset = 0s) {
    return duration_cast<std::chrono::seconds>
            ((std::chrono::system_clock::now() - offset).time_since_epoch()).count() << 22;
}

struct guild_user_msg_cache {
    const chr::seconds save_time;
    dpp::cluster* const owner;
    const snowflake guild_id;

    dpp::cache<message> messages;

    std::shared_mutex mi_mutex;
    std::set<snowflake> message_ids;
    std::multimap<snowflake,
            std::priority_queue<dpp::snowflake, std::vector<dpp::snowflake>, std::greater<>>> user_msgs;

    guild_user_msg_cache(dpp::guild* guild, dpp::cluster* ptr,
                         chr::seconds t = duration_cast<chr::seconds>(chr::weeks{1})) :
            save_time(t), owner(ptr), guild_id(guild->id) {

        auto channels = guild->channels;

        //Probably don't need to remove duplicates tbh
        ran::sort(channels);
        auto it = ran::unique(channels);
        channels.erase(it.begin(), it.end());

        for (auto channel_id: channels ) {
            owner->messages_get(channel_id, 0, 0, cur_msg_time(save_time), 100,
            [this](const auto &msg_event) {
                if (msg_event.is_error()) {
                    owner->log(dpp::ll_error,
                               fmt::format("{}, Source: guild_user_msg_cache call to messages_get",
                                           msg_event.get_error().message));
                }
                else {
                    auto map = msg_event.template get<dpp::message_map>();
                    for (const auto& [id, msg]:  map) {
                        mt_insert(msg);
                    }
                }
            });
        }
    }

    void mt_insert(const message &msg, bool update = false) {
        constexpr auto user_msg_cached_count = 5;

        if (message_ids.emplace(msg.id).second) {
            auto new_msg = new message{msg};
            messages.store(new_msg); //move here has no benefit apparently

            if (!update) {
                auto lock = std::unique_lock(mi_mutex);

                auto it = user_msgs.find(msg.author.id);

                if (it == user_msgs.end()) {
                    it = user_msgs.emplace(std::piecewise_construct,
                                           std::forward_as_tuple(msg.author.id),
                                           std::forward_as_tuple(std::greater<>()));
                }
                it->second.push(msg.id);

                if (it->second.size() > user_msg_cached_count) {
                    auto popped = it->second.top();
                    it->second.pop();
                    message_ids.erase(popped);
                    messages.remove(messages.find(popped));
                }
            }
        }
    }

    void mt_remove_user(snowflake user) {
        auto lock = std::unique_lock(mi_mutex);
        user_msgs.erase(user);
    }

    ~guild_user_msg_cache() {
        auto lock = std::unique_lock{mi_mutex};
        for (const auto& [key, value]: messages.get_container()) {
            messages.remove(value);
        }
    }
};

//The discriminators are the role ids for the obfs_dojo roles associated with these
enum Dojo_Belt: ::uint64_t {
    White = 918327547450765352,
    Yellow = 929080104754245674,
    Green = 929080211205668894,
    Blue = 1036453240608411678,
    Red = 929080487736119346,
    Black = 929080635904110663
};

const static std::vector<Dojo_Belt> BELTS {
    White, Yellow, Green, Blue, Red, Black
};

const static std::unordered_map<Dojo_Belt, Dojo_Belt> PLUS_ONE {
    {White, Yellow},
    {Yellow, Green},
    {Green, Blue},
    {Blue, Red},
    {Red, Black},
    {Black, Black}
};

const static std::unordered_map<Dojo_Belt, std::string> NAME {
        {White, "White"},
        {Yellow, "Yellow"},
        {Green, "Green"},
        {Blue, "Blue"},
        {Red, "Red"},
        {Black, "Black"}
};

inline coc_vec belt_options() {
    coc_vec vec;
    ran::transform(NAME, std::back_inserter(vec), [](auto& y){
        return dpp::command_option_choice{y.second, dpp::command_value{static_cast<snowflake>(y.first)}};
    });
    return vec;
}

struct guild_dojo_info {
    guild_dojo_info(dpp::cluster* a, snowflake b) : owner(a), guild_id(b) {}

    dpp::cluster* const owner;
    const snowflake guild_id;

    std::shared_mutex uc_mutex;
    std::multimap<snowflake, snowflake> user_categories;
    std::map<snowflake, snowflake> categories_to_users;

};

struct guild_channel_sender {
    explicit guild_channel_sender(dpp::cluster* p_owner, snowflake p_channel = 0)
    : owner(p_owner), channel(p_channel) {}

    dpp::cluster* owner;
    snowflake channel = 0;

    void send(message& msg, const dpp::command_completion_event_t& callback = dpp::utility::log_error()) const {
        if (channel) {
            msg.channel_id = channel;
            owner->message_create(msg, callback);
        }
    }
};