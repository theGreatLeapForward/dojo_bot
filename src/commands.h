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
void sc_belt(const slashcommand_t&, guild_state&);
void ctx_belt_upgrade(const user_context_menu_t&, guild_state&);

template<typename T>
using cmd_func = std::function<void(T, guild_state&)>;

template<typename T>
using func_map = std::unordered_map<std::string, cmd_func<T>>;

template<typename T, typename ... U>
concept IsAnyOf = (std::same_as<T, U> || ...);

template <typename ...Args>
struct command_handler {
    command_handler(dpp::cluster* owner_param, snowflake id_param, guild_state* ref_param)
            : owner(owner_param), id(id_param), ref(ref_param) {

        static_assert(IsAnyOf<slashcommand_t, Args...>);

        if (!std::get<func_map<slashcommand_t>>(cmd_funcs).emplace(help.name, cmd_func<slashcommand_t>{sc_help}).second) {
            owner->log(dpp::ll_error, "EMPLACE FAILURE FOR COMMAND HELP");
        }

        owner->log(dpp::ll_info, fmt::format("cmd_handler created for {}", id));
    }

    dpp::cluster* const owner;
    const snowflake id;
    guild_state* ref;

    std::shared_mutex cmds_mutex;
    slashcommand help = slashcommand{"help", "get help for the bot, or a specific command", owner->me.id}.
            add_option(dpp::command_option{dpp::co_string, "command", "[Optional] command to get help for"});
    std::tuple<func_map<Args> ...> cmd_funcs;

    std::vector<slashcommand> bulk_register;

    command_handler& default_cmds() {
        using sc = slashcommand;
        using co = dpp::command_option;

        add_command<slashcommand_t>(
                sc{"rolelist", "get all users with specified role", owner->me.id}.
                        add_option(co{dpp::co_role, "role", "role to get users with", true}),
                sc_rolelist);
        add_command<slashcommand_t>(
                sc{"user-activity", "what are you up to?", owner->me.id}.
                        add_option(co{dpp::co_user, "user", "user who's activity to check. Defaults to you", false}),
                sc_user_activity);
        add_command<user_context_menu_t>(
                sc{"activity", "", owner->me.id}.
                        set_type(dpp::ctxm_user),
                ctx_user_activity);
        add_command<slashcommand_t>(
                sc{"channel-activity", "what is going on in here?", owner->me.id}.
                        add_option(co{dpp::co_channel, "channel", "channel who's activity to check. Defaults to the current channel", false}.
                        add_channel_type(dpp::CHANNEL_TEXT)),
                sc_channel_activity);

        add_command<slashcommand_t>(
                sc("debug", "running this a lot will crash the bot", owner->me.id).
                        set_default_permissions(dpp::p_administrator),
                sc_debug);
        add_command<slashcommand_t>(
                sc("stop", "[EMERGENCY]: stops the bot", owner->me.id).
                        set_default_permissions(dpp::p_administrator),
                sc_restart);
        add_command<slashcommand_t>(
                sc{"guides", "retrieves a guide", owner->me.id}.
                        add_option(add_choices(co{dpp::co_string, "guide", "which guide to retrieve", true}, guilds_options())),
                sc_guides);

        return *this;
    }

    command_handler &dojo_cmds() {
        using sc = slashcommand;
        using co = dpp::command_option;
        using coc = dpp::command_option_choice;

        auto ownership_add = co{dpp::co_sub_command, "add", "adds a user as an owner of a category"}.
                add_option(co{dpp::co_channel, "category", "category to add user as owner of", true}.
                add_channel_type(dpp::CHANNEL_CATEGORY)).
                add_option(co{dpp::co_user, "user", "user to add as owner of category", true});

        auto ownership_remove = co{dpp::co_sub_command, "remove", "removes a user as an owner of a category"}.
                add_option(co{dpp::co_channel, "category", "category to remove user as owner of", true}.
                add_channel_type(dpp::CHANNEL_CATEGORY)).
                add_option(co{dpp::co_user, "user", "user to remove as owner of category", true});

        auto ownership_clear_ch = co{dpp::co_sub_command, "clear-category", "clears a category of owner"}.
                add_option(co{dpp::co_channel, "category", "category to clear of ownership", true}.
                add_channel_type(dpp::CHANNEL_CATEGORY));

        auto ownership_clear_usr = co{dpp::co_sub_command, "clear-user", "clears a user of all category ownership"}.
                add_option(co{dpp::co_user, "user", "user to clear ownership of categories", true});

        auto ownership = co{dpp::co_sub_command_group, "ownership", "modifies the ownership of a category"}.
                add_option(ownership_add).add_option(ownership_remove).add_option(ownership_clear_usr).add_option(ownership_clear_ch);

        auto archive_add = co{dpp::co_sub_command, "archive", "archives a category"}.
                add_option(co{dpp::co_channel, "category", "category to archive", true}).
                add_channel_type(dpp::CHANNEL_CATEGORY);

        auto archive_remove = co{dpp::co_sub_command, "un-archive", "un-archives a category"}.
                add_option(co{dpp::co_channel, "category", "category to un-archive", true}).
                add_channel_type(dpp::CHANNEL_CATEGORY);

        auto archive = co{dpp::co_sub_command_group, "archive", "modifies the archive-status of a category"}.
                add_option(archive_add).add_option(archive_remove);

        add_command<slashcommand_t>(
                sc{"category", "category modification commands", owner->me.id}.
                        add_option(ownership).
                        add_option(archive).
                        set_default_permissions(dpp::p_manage_guild),
                sc_category);

        auto start = co{dpp::co_sub_command, "start", "start a new score counting helper for a game"}.
                add_option(co{dpp::co_string, "id", "the id of the game. Used for later lookup. Can be whatever you want as long as it is unique.", true}).
                add_option(co{dpp::co_string, "name", "[Optional] the name of the game"});

        auto finish = co{dpp::co_sub_command, "finish", "mark a score counting helper game as finished"}.
                add_option(co{dpp::co_string, "id", "the id of the game to mark as finished", true});

        add_command<slashcommand_t>(
                sc{"score-count-helper", "score counting help", owner->me.id}.
                add_option(start).
                add_option(finish),
                sc_scorecounting);

        add_command<slashcommand_t>(
                sc{"sc", "score counting help", owner->me.id}.
                add_option(start).
                add_option(finish),
                sc_scorecounting);

        auto award = co{dpp::co_sub_command, "award", "award a belt to a user. Removes previous belts, if any"}.
                add_option(co{dpp::co_user, "user", "the user to award the belt to", true}).
                add_option(add_choices(co{dpp::co_string, "belt", "The belt to award. Defaults to increasing the user's \"belt level\" by 1", false}, belt_options()));

        auto revoke = co{dpp::co_sub_command, "revoke", "revokes a user's belt"}.
                add_option(co{dpp::co_user, "user", "the user to revoke the belt from", true});

        auto requirements = co{dpp::co_sub_command, "requirements", "gets the requirements for a specific belt color"}.
                add_option(add_choices(co{dpp::co_string, "color", "the color of the belt", true}, belt_options()));

        add_command<slashcommand_t>(
                sc{"belt", "belt management commands", owner->me.id}.
                add_option(award).
                add_option(revoke).
                        add_option(requirements).
                set_default_permissions(dpp::p_manage_roles),
                sc_belt);

        add_command<user_context_menu_t>(sc{"belt-upgrade", "", owner->me.id}.
            set_type(dpp::ctxm_user),
            ctx_belt_upgrade);

        return *this;
    }

    //This should be called after other funcs. Clears the list of registered commands to allow to b called multiple times
    void create() {
        owner->guild_bulk_command_create(bulk_register, id);
        owner->guild_command_create(help, id);
        {
            auto lock = std::unique_lock(cmds_mutex);
            bulk_register.clear();
        }
    }

    template<typename T>
    requires IsAnyOf<T, Args...>
    command_handler& add_command(dpp::slashcommand cmd, cmd_func<T> func) {

        if (std::get<func_map<T>>(cmd_funcs).emplace(cmd.name, std::move(func)).second) {
            {
                auto lock = std::unique_lock(cmds_mutex);
                help.options[0].add_choice(dpp::command_option_choice{cmd.name, cmd.id});
                bulk_register.emplace_back(std::move(cmd));
            }
        }

        return *this;
    }

    template<typename T>
    requires IsAnyOf<T, Args...>
    inline void handle(const T& event) {
        std::invoke(std::get<func_map<T>>(cmd_funcs).at(event.command.get_command_name()), event, *ref);
    }
};

struct guild_state : dpp::managed {
    guild_state(dpp::cluster& p_bot, std::promise<void>& exec_stop, dpp::guild* p_guild)
    : dpp::managed(p_guild->id), bot(p_bot), exec_stop(exec_stop),
      msg_cache(p_guild, &bot), cmd_handler(&bot, id, this),
      log_channel(&bot), welcomer(&bot) {
        bot.log(dpp::ll_info, fmt::format("guild_state created for guild {}", id));
    }

    dpp::cluster& bot;
    std::promise<void>& exec_stop;
    guild_user_msg_cache msg_cache;
    command_handler<dpp::slashcommand_t, dpp::user_context_menu_t, dpp::message_context_menu_t> cmd_handler;
    guild_channel_sender log_channel;
    guild_channel_sender welcomer;
    std::optional<guild_dojo_info> dojo_info {};
};

void category_clear(snowflake, guild_state&);
void user_category_clear(snowflake, guild_state&);
