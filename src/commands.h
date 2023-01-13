//
// Created by thegreatleapforward on 11/01/23.
//

#pragma once
#include "backend.h"
#include "guides.h"
#include <spdlog/fmt/fmt.h>
#include <concepts>
#include <dpp/dpp.h>
#include <functional>

using namespace std::chrono_literals;
namespace ran = std::ranges;
namespace chr = std::chrono;
using dpp::slashcommand;
using dpp::snowflake;
using dpp::message;
using dpp::slashcommand_t;
using dpp::user_context_menu_t;
using dpp::message_context_menu_t;

inline dpp::command_option add_choices(dpp::command_option option, coc_vec&& choices) {
    if (option.autocomplete) {
        throw dpp::logic_exception("Can't set autocomplete=true if choices exist in the command_option");
    }
    std::move(choices.begin(), choices.end(), std::back_inserter(option.choices));
    return option;
}

struct guild_state;

void sc_rolelist(const slashcommand_t&, guild_state&);
void sc_channel_activity(const slashcommand_t&, guild_state&);
void sc_user_activity(const slashcommand_t&, guild_state&);
void ctx_user_activity(const user_context_menu_t&, guild_state&);
void sc_category(const slashcommand_t&, guild_state&);
void sc_debug(const slashcommand_t&, guild_state&);
void sc_restart(const slashcommand_t&, guild_state&);
void sc_guides(const slashcommand_t&, guild_state&);
void sc_scorecounting(const slashcommand_t&, guild_state&);
void sc_help(const slashcommand_t&, guild_state&);

template<typename T>
using cmd_func = std::function<void(const T&, guild_state&)>;

template<typename T>
using func_map = std::unordered_map<snowflake, cmd_func<T>>;

template<typename T, typename ... U>
concept IsAnyOf = (std::same_as<T, U> || ...);

template <typename ...Args>
struct command_handler {
    command_handler(dpp::cluster* owner_param, snowflake id_param, guild_state* ref_param)
            : owner(owner_param), id(id_param), ref(ref_param) {
        owner->log(dpp::ll_info, fmt::format("command_handler created for {}", id));
    }

    dpp::cluster* const owner;
    const snowflake id;
    guild_state* ref;

    std::shared_mutex cmds_mutex;
    std::tuple<func_map<Args> ...> cmd_funcs;
    std::map<snowflake, slashcommand> commands;
    slashcommand help = slashcommand{"help", "get help for the bot, or a specific command", id}.
        add_option(dpp::command_option{dpp::co_string, "command", "[Optional] command to get help for"});

    std::atomic<bool> called = false;

    command_handler& default_cmds() {
        using sc = slashcommand;
        using co = dpp::command_option;

        add_command<slashcommand_t>(
                sc{"rolelist", "get all users with specified role", id}.
                        add_option(co{dpp::co_role, "role", "role to get users with", true}),
                sc_rolelist);
        add_command<slashcommand_t>(
                sc{"user-activity", "what are you up to?", id}.
                        add_option(co{dpp::co_user, "user", "user who's activity to check", false}),
                sc_user_activity);
        add_command<user_context_menu_t>(
                sc{"activity", "", id}.
                        set_type(dpp::ctxm_user),
                ctx_user_activity);
        add_command<slashcommand_t>(
                sc{"channel-activity", "what is going on in here?", id}.
                        add_option(co{dpp::co_channel, "channel", "channel who's activity to check", false}),
                sc_channel_activity);

        auto ownership_add = co{dpp::co_sub_command, "add", "adds a user as an owner of a category"}.
                add_option(co{dpp::co_channel, "category", "category to add user as owner of"}.
                add_channel_type(dpp::CHANNEL_CATEGORY)).
                add_option(co{dpp::co_user, "user", "user to add as owner of category"});

        auto ownership_remove = co{dpp::co_sub_command, "remove", "removes a user as an owner of a category"}.
                add_option(co{dpp::co_channel, "category", "category to remove user as owner of"}.
                add_channel_type(dpp::CHANNEL_CATEGORY)).
                add_option(co{dpp::co_user, "user", "user to remove as owner of category"});

        auto ownership_clear_ch = co{dpp::co_sub_command, "clear-category", "clears a category of owner"}.
                add_option(co{dpp::co_channel, "category", "category to clear of ownership"}.
                add_channel_type(dpp::CHANNEL_CATEGORY));

        auto ownership_clear_usr = co{dpp::co_sub_command, "clear-user", "clears a user of all category ownership"}.
                add_option(co{dpp::co_user, "user", "user to clear ownership of categories"});

        auto ownership = co{dpp::co_sub_command_group, "ownership", "modifies the ownership of a category"}.
                add_option(ownership_add).add_option(ownership_remove).add_option(ownership_clear_usr).add_option(ownership_clear_ch);

        auto archive_add = co{dpp::co_sub_command, "archive", "archives a category"}.
                add_option(co{dpp::co_channel, "category", "category to archive"}).
                add_channel_type(dpp::CHANNEL_CATEGORY);

        auto archive_remove = co{dpp::co_sub_command, "un-archive", "un-archives a category"}.
                add_option(co{dpp::co_channel, "category", "category to un-archive"}).
                add_channel_type(dpp::CHANNEL_CATEGORY);

        auto archive = co{dpp::co_sub_command_group, "archive", "modifies the archive-status of a category"}.
                add_option(archive_add).add_option(archive_remove);

        add_command<slashcommand_t>(
                sc{"category", "category modification commands", id}.
                        add_option(ownership).
                        add_option(archive).
                        set_default_permissions(dpp::p_manage_guild),
                sc_category);
        add_command<slashcommand_t>(
                sc("debug", "running this a lot will crash the bot", id).
                        set_default_permissions(dpp::p_administrator),
                sc_debug);
        add_command<slashcommand_t>(
                sc("stop", "[EMERGENCY]: stops the bot", id).
                        set_default_permissions(dpp::p_administrator),
                sc_restart);
        add_command<slashcommand_t>(
                sc{"guides", "retrieves a guide", id}.
                        add_option(add_choices(co{dpp::co_string, "guide", "which guide to retrieve", true}, guilds_options())),
                sc_guides);

        return *this;
    }

    command_handler &dojo_cmds() {
        using sc = slashcommand;

        add_command<slashcommand_t>(sc{"score-count-helper", "score counting help", id}, sc_scorecounting);

        return *this;
    }

    void register_cmds() {
        if (!called) {
            called = true;

            for (auto& [command_id, command]: commands) {
                help.options[0].add_choice(dpp::command_option_choice{command.name, command.id});
                owner->guild_command_create(command, id);
            }
            owner->guild_command_create(help, id);
        }
        else {
            throw dpp::logic_exception(fmt::format(
                    "register_cmds called a 2nd time on a command handler for guild {}", id));
        }
    }

    template<typename T>
    requires IsAnyOf<T, Args...>
    command_handler& add_command(const dpp::slashcommand& cmd, cmd_func<T>&& func) {
        {
            auto lock = std::unique_lock(cmds_mutex);
            commands.emplace(cmd.id, cmd);
            std::get<func_map<T>>(cmd_funcs).emplace(cmd.id, func);
        }

        if (!called) {
            owner->guild_command_create(cmd, id);
            owner->guild_command_create(help, id); //update help
        }
        return *this;
    }

    template<typename T>
    requires IsAnyOf<T, Args...>
    inline void handle(const T& event) {
        std::invoke(std::get<func_map<T>>(cmd_funcs).at(event.command.id), event, *ref);
    }
};

using interaction_handler = command_handler<dpp::slashcommand_t,
        dpp::user_context_menu_t, dpp::message_context_menu_t>;

struct guild_state {
    guild_state(dpp::cluster& p_bot, std::promise<void>& exec_stop, dpp::guild* p_guild)
    : bot(p_bot), exec_stop(exec_stop), guild_id(p_guild->id),
    msg_cache(p_guild, &bot), command_handler(&bot, bot.me.id, this),
    log_channel(&bot), welcomer(&bot) {}

    dpp::cluster& bot;
    std::promise<void>& exec_stop;
    snowflake guild_id;
    guild_user_msg_cache msg_cache;
    interaction_handler command_handler;
    guild_channel_sender log_channel;
    guild_channel_sender welcomer;
    std::optional<guild_dojo_info> dojo_info {};
};

void category_clear(snowflake, guild_state&);
void user_category_clear(snowflake, guild_state&);
