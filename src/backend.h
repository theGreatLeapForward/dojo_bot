//
// Created by thegreatleapforward on 11/01/23.
//

#pragma once

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

template<typename T>
using id_map = std::unordered_map<snowflake, T>;

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
            guild_id(guild->id), owner(ptr), save_time(t) {

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
                                        owner->log(dpp::ll_debug, "No error in call to messages_get");

                                        auto map = msg_event.template get<dpp::message_map>();
                                        for (const auto& [id, msg]:  map) {
                                            mt_insert(msg);
                                        }
                                    }
                                });
        }
    }

    void mt_insert(const message &msg) {
        constexpr auto user_msg_cached_count = 5;

        auto lock = std::unique_lock(mi_mutex);

        if (message_ids.emplace(msg.id).second) {
            auto new_msg = new message{msg};
            messages.store(new_msg); //move here has no benefit apparently
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

    ~guild_user_msg_cache() {
        auto lock = std::unique_lock{mi_mutex};
        for (const auto& [key, value]: messages.get_container()) {
            messages.remove(value);
        }
    }
};

using gumc_map = id_map<guild_user_msg_cache>;

struct dojo_info {
    dpp::cluster* const owner;
    const snowflake guild_id;

    std::shared_mutex uc_mutex;
    std::multimap<snowflake, snowflake> user_categories;
    std::map<snowflake, snowflake> categories_to_users;

private:
    std::shared_mutex mnc_mutex;
    std::vector<std::pair<snowflake, std::string>> member_names_cache;

public:
    dojo_info(dpp::cluster* param_owner, snowflake param_guild_id) : owner(param_owner), guild_id(param_guild_id) {

        auto& members = dpp::find_guild(guild_id)->members;
        member_names_cache.resize(members.size()); //Apparently transform will literally index into rando ass memory
        //TODO: Count nicknames here as well
        ran::transform(members.cbegin(), members.cend(),
                       member_names_cache.begin(), [](auto& x){
                    auto pair = std::make_pair(x.first, x.second.get_user()->username);
                    return pair;
                });

        owner->channels_get(guild_id, [this](const dpp::confirmation_callback_t& event){
            if (!event.is_error()) {
                auto map = event.get<dpp::channel_map>();
                for (const auto& [_, channel]: map) {
                    mt_check_channel(channel);
                }
            }
            else {
                owner->log(dpp::ll_error,
                           fmt::format("{}, Source: dojo_info constructor call to channels_get",
                                       event.get_error().message));
            }
        });
    }

    void mt_check_channel(const dpp::channel& channel, bool del = false) {
        if (channel.is_category()) {
            if (!del) {
                for (const auto & [usr_id, name]: member_names_cache) {
                    if (std::regex_search(channel.name.cbegin(), channel.name.cend(),
                                          std::basic_regex(name))) {
                        auto lock = std::unique_lock(uc_mutex);
                        user_categories.emplace(usr_id, channel.id);
                        categories_to_users.emplace(channel.id, usr_id);
                        break;
                    }
                }
            }
            else {
                auto it = categories_to_users.find(channel.id);
                if (it != categories_to_users.end()) {
                    auto lock = std::unique_lock(uc_mutex);
                    user_categories.erase((*it).second);
                    categories_to_users.erase(it);
                }
            }
        }
    }

    //NO DUPLICATE CHECKING !!! ADDING 8000 DUPLICATES WILL KILL PERF
    void mt_member_insert(const dpp::user& user) {
        auto lock = std::unique_lock(mnc_mutex);
        member_names_cache.emplace_back(user.id, user.username);
    }

    void mt_remove_member(const snowflake id) {
        {
            auto lock = std::unique_lock(uc_mutex);
            categories_to_users.erase(user_categories.find(id));
            user_categories.erase(id);
        }
        {
            auto lock = std::unique_lock(mnc_mutex);
            erase_if(member_names_cache,
                     [id](auto& y){return y.first == id;});
        }
    }

    //USER IS FIRST AHAHW
    void override(snowflake user_id, snowflake channel_id) {
        auto lock = std::unique_lock(uc_mutex);
        user_categories.emplace(user_id, channel_id);
        categories_to_users.emplace(channel_id, user_id);
    }
};

using dj_map = id_map<dojo_info>;

struct guild_logger {
    dpp::cluster* owner;
    snowflake channel;

    void log(message& msg, const dpp::command_completion_event_t& callback = dpp::utility::log_error()) const {
        msg.channel_id = channel;
        owner->message_create(msg, callback);
    }
};

using gl_map = id_map<guild_logger>;

template<typename ...Args>
struct command_handler;
using interaction_handler = command_handler<dpp::slashcommand_t, dpp::user_context_menu_t, dpp::message_context_menu_t>;
using ch_map = id_map<interaction_handler>;

struct state_ref {
    dpp::cluster& bot;
    std::promise<void>& exec_stop;
    id_vec& dojo_ids;
    id_vec& guild_ids;
    gumc_map& msg_cache;
    dj_map& dojo_infos;
    ch_map& command_handlers;
    gl_map& log_channels;
};