//
// Created by thegreatleapforward on 03/01/23.
//
#include <dpp/dpp.h>
#include <spdlog/fmt/fmt.h>
#include <set>
#include <map>
#include <ranges>
#include <regex>

using namespace std::chrono_literals;
namespace ran = std::ranges;
namespace chr = std::chrono;
struct state_ref;
using sc_func = std::function<void(const dpp::slashcommand_t&, state_ref&)>;
using ucm_func = std::function<void(const dpp::user_context_menu_t&, state_ref&)>;
using mcm_func = std::function<void(const dpp::message_context_menu_t&, state_ref&)>;
using func_variant = std::variant<sc_func, ucm_func, mcm_func>;
using dpp::snowflake;
using dpp::message;

template<class Rep, class Period>
auto cur_msg_time(const std::chrono::duration<Rep, Period>& offset = 0s) {
    return duration_cast<std::chrono::seconds>
            ((std::chrono::system_clock::now() - offset).time_since_epoch()).count() << 22;
}

struct guild_msg_cache {
    chr::seconds save_time;
    dpp::cluster* owner;
    snowflake guild_id;
    dpp::cache<message> messages;

    std::shared_mutex mi_mutex;
    std::set<snowflake> message_ids;

    //Perhaps these should be std::multimap<snowflake, message*>
    // as to reduce the cost of iterating over messages
    std::multimap<snowflake, message*> user_msgs;
    std::multimap<snowflake, message*> channel_msgs;

    guild_msg_cache(dpp::guild* guild, dpp::cluster* ptr,
                    chr::seconds t = duration_cast<chr::seconds>(chr::weeks{1})) :
    guild_id(guild->id), owner(ptr), save_time(t) {

        auto channels = guild->channels;
        ran::sort(channels);
        auto it = ran::unique(channels);
        channels.erase(it.begin(), it.end());

        const auto time = cur_msg_time(this->save_time);

        for (auto channel_id: channels ) {
            this->owner->messages_get(channel_id, 0, 0, time, 100,
                              [this](const auto &msg_event) {
                if (msg_event.is_error()) {
                    this->owner->log(dpp::ll_error, msg_event.get_error().message);
                }
                else {
                    auto map = msg_event.template get<dpp::message_map>();
                    /*if (!map.size()) {
                        this->owner->log(dpp::ll_error,
                                         "No Messages Found for channel: " + std::to_string(channel_id));
                    }*/
                    for (const auto &[message_id, message]: map) {
                        this->mt_insert(message);
                    }
                }
            });
        }
        //garbage collection
        this->owner->start_timer(dpp::timer_callback_t{[this](auto param){
            this->gc();
        }}, duration_cast<std::chrono::seconds>(std::chrono::days{1}).count());
    }

    void mt_insert(const message &msg) {
        auto lock = std::unique_lock(mi_mutex);

        if (this->message_ids.emplace(msg.id).second) {
            auto new_msg = new message{msg};
            this->messages.store(new_msg); //move here has no benefit apparently
            this->user_msgs.insert({msg.author.id, new_msg});
            this->channel_msgs.insert({msg.channel_id, new_msg});
        }
    }

    void gc() { //TODO this doesn't work lol
        const auto time = cur_msg_time(this->save_time);
        const auto it = this->message_ids.lower_bound(time);

        auto range = ran::subrange(this->message_ids.begin(), it);
        for (auto id: range) {
            auto msg_ptr = this->messages.find(id);

            auto [usr_b, usr_e] = this->user_msgs.equal_range(msg_ptr->author.id);
            for (; usr_b != usr_e; ++usr_b) {
                if ((*usr_b).second == msg_ptr) break;
            }
            this->user_msgs.erase(usr_b);

            auto [channel_b, channel_e] = this->channel_msgs.equal_range(msg_ptr->channel_id);
            for (; channel_b != channel_e; ++channel_b) {
                if ((*channel_b).second == msg_ptr) break;
            }
            this->channel_msgs.erase(channel_b);

            this->messages.remove(msg_ptr);
        }

        this->message_ids.erase(this->message_ids.begin(), it);
    }

    ~guild_msg_cache() {
        auto lock = std::unique_lock{this->mi_mutex};
    }
};

struct dojo_info {
    dpp::cluster* owner;
    snowflake guild_id;

    std::shared_mutex uc_mutex;
    std::multimap<snowflake, snowflake> user_categories;
    std::map<snowflake, snowflake> categories_to_users;

    std::shared_mutex mnc_mutex;
    std::vector<std::pair<snowflake, std::string>> member_names_cache;

    dojo_info(dpp::cluster* owner, snowflake guild_id) : owner(owner), guild_id(guild_id) {

        auto& members = dpp::find_guild(this->guild_id)->members;
        this->member_names_cache.resize(members.size()); //Apparently transform will literally index into rando ass memory
        //TODO: Count nicknames here as well
        ran::transform(members.cbegin(), members.cend(),
                       this->member_names_cache.begin(), [](auto& x){
            auto pair = std::make_pair(x.first, x.second.get_user()->username);
            return pair;
        });

        this->owner->channels_get(this->guild_id, [this](const dpp::confirmation_callback_t& event){
            if (!event.is_error()) {
                auto map = event.get<dpp::channel_map>();
                for (const auto& [_, channel]: map) {
                    this->mt_check_channel(channel);
                }
            }
            else {
                this->owner->log(dpp::ll_error, event.get_error().message);
            }
        });
    }

    void mt_check_channel(const dpp::channel& channel, bool del = false) {
        if (channel.is_category()) {
            if (!del) {
                for (const auto & [usr_id, name]: this->member_names_cache) {
                    if (std::regex_search(channel.name.cbegin(), channel.name.cend(),
                                          std::basic_regex(name))) {
                        auto lock = std::unique_lock(this->uc_mutex);
                        user_categories.emplace(usr_id, channel.id);
                        categories_to_users.emplace(channel.id, usr_id);
                        break;
                    }
                }
            }
            else {
                auto it = this->categories_to_users.find(channel.id);
                if (it != this->categories_to_users.end()) {
                    auto lock = std::unique_lock(this->uc_mutex);
                    this->user_categories.erase((*it).second);
                    this->categories_to_users.erase(it);
                }
            }
        }
    }

    //NO DUPLICATE CHECKING !!! ADDING 8000 DUPLICATES WILL KILL PERF
    void mt_member_insert(const dpp::user& user) {
        auto lock = std::unique_lock(this->mnc_mutex);
        this->member_names_cache.emplace_back(user.id, user.username);
    }

    void remove_member(const snowflake id) {
        {
            auto lock = std::unique_lock(this->uc_mutex);
            this->categories_to_users.erase(this->user_categories.find(id));
            this->user_categories.erase(id);
        }
        {
            auto lock = std::unique_lock(this->mnc_mutex);
            erase_if(this->member_names_cache,
                     [id](auto& y){return y.first == id;});
        }
    }

    //USER IS FIRST AHAHW
    void override(snowflake user_id, snowflake channel_id) {
        auto lock = std::unique_lock(this->uc_mutex);
        user_categories.emplace(user_id, channel_id);
        categories_to_users.emplace(channel_id, user_id);
    }
};

struct command_handler {
    dpp::cluster* owner;
    snowflake guild_id;
    std::vector<std::string> command_names;
    std::map<std::string, std::pair<dpp::slashcommand, sc_func>> sc_map;
    std::map<std::string, std::pair<dpp::slashcommand, ucm_func>> ucm_map;
    std::map<std::string, std::pair<dpp::slashcommand, mcm_func>> mcm_map;

    command_handler(dpp::cluster* owner,
                    snowflake guild_id,
                    std::vector<std::pair<dpp::slashcommand, func_variant>>&& create)
                    : owner(owner), guild_id(guild_id) {

        this->command_names.resize(create.size());
        for (auto& [sc, func]: create) {
            auto name = sc.name;
            this->command_names.push_back(name);
            this->owner->guild_command_create(sc, this->guild_id);

            switch (sc.type) {
                case dpp::ctxm_chat_input:
                    this->sc_map.insert({std::move(name),
                                         std::make_pair(std::move(sc),std::get<sc_func>(std::move(func)))});
                    break;
                case dpp::ctxm_user:
                    this->ucm_map.insert({std::move(name),
                                         std::make_pair(std::move(sc),std::get<ucm_func>(std::move(func)))});
                    break;
                case dpp::ctxm_message:
                    this->mcm_map.insert({std::move(name),
                                         std::make_pair(std::move(sc),std::get<mcm_func>(std::move(func)))});
                    break;
                default:
                    owner->log(dpp::ll_error, fmt::format("Invalid context menu type: {}", sc.name));
            }
        }
    }
};

struct state_ref {
    dpp::cluster& bot;
    std::promise<void>& exec_stop;
    const std::vector<snowflake>& dojo_ids;
    std::unordered_map<dpp::snowflake, dojo_info>& dojo_infos;
};

void rolelist(const dpp::slashcommand_t& event, state_ref&) {
    auto role_id = std::get<snowflake>(event.get_parameter("role"));
    auto embed = dpp::embed().
            set_title(fmt::format("All users with role {}", dpp::find_role(role_id)->get_mention()));

    auto& members = dpp::find_guild(event.command.guild_id)->members;
    for (const auto& [member_id, member]: members) {
        if (ran::find(member.roles, role_id) != member.roles.end()) {
            embed.add_field(member.get_mention(), "todo", true);
        }
    }
    event.reply(message{event.command.channel_id, embed});
}

void sc_user_activity(const dpp::slashcommand_t& event, state_ref&) {
    auto embed = dpp::embed().set_title("Todo");

    event.reply(message{event.command.channel_id, embed});
}

void sc_channel_activity(const dpp::slashcommand_t& event, state_ref& ref) {
    event.thinking(true);
    constexpr int preview_size = 5;
    constexpr int max_msg_size = 100;

    ref.bot.messages_get(event.command.channel_id, 0, 0, 0, preview_size,
                         [&event, &ref](const dpp::confirmation_callback_t& conf){
        if (!conf.is_error()) {
            auto param = event.get_parameter("channel");
            auto id = event.command.channel_id;
            if (std::holds_alternative<snowflake>(param)) {
                try {
                    id = std::get<snowflake>(param);
                }
                catch (std::bad_variant_access& ex) {
                    ref.bot.log(dpp::ll_error, ex.what());
                }
            }
            auto channel = dpp::find_channel(id);
            if (channel->is_category()) {
                event.reply("Categories are not yet supported."); //TODO
                return;
            }

            auto embed = dpp::embed().set_color(0xA020F0).
                    set_title(fmt::format("Recent activity in channel {}", channel->get_mention()));

            auto& parent_name = dpp::find_channel(channel->parent_id)->name;
            auto it = ref.dojo_infos.find(event.command.guild_id);
            if (it != ref.dojo_infos.end()) {
                auto& cat = it->second.categories_to_users;
                auto cat_it = cat.find(channel->parent_id);
                if (cat_it != cat.end()) {
                    embed.add_field("Channel ownership",
                                    fmt::format("Channel in category {} owned by {}",
                                                parent_name,
                                                dpp::find_user(cat_it->second)->get_mention()));
                }
                else {
                    embed.add_field("Channel ownership",
                                    fmt::format("Channel in unowned category {}", parent_name));
                }
            }
            else {
                embed.add_field("Category", fmt::format("Channel in category {}", parent_name));
            }

            embed.add_field("Channel preview", fmt::format("Most recent messages sent in {}", channel->name));
            //TODO message preview
            auto map = conf.get<dpp::message_map>();
            std::vector<message> out;
            out.reserve(map.size());
            ran::transform(std::move(map), out.begin(), [](auto& y){return y.second;});
            ran::sort(out, [](auto& x, auto& y){return x.id > y.id;});
            for (auto& msg: out) {
                if (msg.content.length() > max_msg_size) {
                    msg.content.resize(max_msg_size);
                    msg.content += "...";
                }

                embed.add_field(fmt::format("From: {}", msg.author.get_mention()),
                                    msg.content);
            }

            event.reply(message{id, embed});
        }
        else {
            ref.bot.log(dpp::ll_error, conf.get_error().message);
        }
    });
}

void ctx_user_activity(const dpp::user_context_menu_t& event, state_ref&) {
    auto embed = dpp::embed().set_title("Todo");

    event.reply(message{event.command.channel_id, embed});
}

void restart(const dpp::slashcommand_t& event, state_ref& ref) {
    event.reply("Am die!");
    ref.exec_stop.set_value();
}

void debug(const dpp::slashcommand_t& event, state_ref& ref) {
    auto guild = dpp::find_guild(event.command.guild_id);
    for (auto channel_id: guild->channels) {
        auto channel = dpp::find_channel(channel_id);
        ref.bot.log(dpp::ll_debug, channel->name);
        ref.bot.log(dpp::ll_info, std::to_string(channel_id));
    }
    event.reply("Debug complete");
}

int main() {
    const std::string filename = "token.txt";
    auto token = std::ifstream{filename};
    std::string BOT_TOKEN;
    if (!token.is_open()) {
        std::cout << "failed to open " << filename << "\n";
        exit(1);
    }
    else {
        token >> BOT_TOKEN;
    }

    dpp::cluster bot(BOT_TOKEN,
                     dpp::i_message_content | dpp::i_default_intents | dpp::i_guild_members);

    bot.on_log(dpp::utility::cout_logger());

    //ALL WRITES SHOULD BE DONE THROUGH mt_ PREFIXED FUNCTIONS
    const std::vector<snowflake> guild_ids {820855382472785921};
    std::unordered_map<snowflake, guild_msg_cache> msg_cache;

    const std::vector<snowflake> dojo_ids {820855382472785921};
    std::unordered_map<snowflake, dojo_info> dojo_infos;

    std::unordered_map<snowflake, command_handler> commands;

    bot.on_guild_create([&guild_ids, &bot, &msg_cache, &dojo_ids, &dojo_infos, &commands](const dpp::guild_create_t& event){
        using sc = dpp::slashcommand;
        using sc = dpp::slashcommand;
        using co = dpp::command_option;
        using p = dpp::permission;

        const auto id = event.created->id;
        if (ran::find(guild_ids, id) == guild_ids.cend()) {
            bot.current_user_leave_guild(id);
        }
        else {
            if (dpp::run_once<struct guild_info_init>()) {

                //Start message cache
                msg_cache.emplace(std::piecewise_construct,
                                  std::forward_as_tuple(id), std::forward_as_tuple(event.created, &bot));

                //Remove all the old commands
                auto oldcommands = bot.guild_commands_get_sync(id);
                for (const auto& [command_id, command]: oldcommands) {
                    bot.guild_command_delete(id, command_id);
                }

                //Add the new commands
                commands.emplace(std::piecewise_construct, std::forward_as_tuple(id), std::forward_as_tuple(&bot, id, std::vector {
                        std::make_pair(sc{"rolelist", "get all users with specified role", bot.me.id}.
                                add_option(co{dpp::co_role, "role", "role", true}),
                                func_variant{rolelist}),
                        std::make_pair(sc{"user-activity", "what are you up to?", bot.me.id}.
                                add_option(co{dpp::co_user, "user", "user", false}),
                                func_variant{sc_user_activity}),
                        std::make_pair(sc{"channel-activity", "what is going on in here?", bot.me.id}.
                                add_option(co{dpp::co_channel, "channel", "channel", false}),
                                func_variant{sc_channel_activity}),
                        std::make_pair(sc{"activity", "what are you up to?", bot.me.id}.
                                set_type(dpp::ctxm_user),
                                func_variant{ctx_user_activity}),
                        std::make_pair(sc("debug", "running this a lot will crash the bot", bot.me.id).
                                set_default_permissions(0),
                                func_variant{debug}),
                        std::make_pair(sc("stop", "[EMERGENCY]: stops the bot", bot.me.id).
                                set_default_permissions(0),
                                func_variant{restart}),
                    }
                ));

                //Is the guild the dojo? We need to cache its special info as well
                if (ran::find(dojo_ids, id) != dojo_ids.end()) {
                    dojo_infos.emplace(std::piecewise_construct,
                                       std::forward_as_tuple(id), std::forward_as_tuple(&bot, id));
                }
            }
        }
    });

    bot.on_channel_create([&dojo_infos](const dpp::channel_create_t& event){
        auto it = dojo_infos.find(event.creating_guild->id);
        if (it != dojo_infos.end()) {
            it->second.mt_check_channel(*event.created);
        }
    });
    bot.on_channel_delete([&dojo_infos](const dpp::channel_delete_t& event){
        auto it = dojo_infos.find(event.deleting_guild->id);
        if (it != dojo_infos.end()) {
            it->second.mt_check_channel(*event.deleted, true);
        }
    });

    bot.on_guild_member_add([&dojo_infos](const dpp::guild_member_add_t& event){
        auto it = dojo_infos.find(event.adding_guild->id);
        if (it != dojo_infos.end()) {
            it->second.mt_member_insert(*event.added.get_user());
        }
    });
    bot.on_guild_member_remove([&dojo_infos](const dpp::guild_member_remove_t& event){
        auto it = dojo_infos.find(event.removing_guild->id);
        if (it != dojo_infos.end()) {
            it->second.remove_member(event.removed->id);
        }
    });

    //probably don't need to actually cache the messages, just keep a running total
    bot.on_message_create([&msg_cache](const dpp::message_create_t& event){
        msg_cache.at(event.msg.guild_id).mt_insert(event.msg);
    });

    std::promise<void> exec_stop;
    auto fut = exec_stop.get_future();

    auto ref = state_ref{.bot = bot, .exec_stop = exec_stop, .dojo_ids = dojo_ids, .dojo_infos = dojo_infos};

    bot.on_slashcommand([&commands, &ref](const dpp::slashcommand_t& event) {
        commands.at(event.command.guild_id).sc_map.at(event.command.get_command_name()).second(event, ref);
    });
    bot.on_user_context_menu([&commands, &ref](const dpp::user_context_menu_t& event){
        commands.at(event.command.guild_id).ucm_map.at(event.command.get_command_name()).second(event, ref);
    });
    bot.on_message_context_menu([&commands, &ref](const dpp::message_context_menu_t& event){
        commands.at(event.command.guild_id).mcm_map.at(event.command.get_command_name()).second(event, ref);
    });

    bot.on_ready([&bot](const dpp::ready_t& event) {
        if (dpp::run_once<struct register_bot_commands>()) {

            for (const auto& [command_id, command]: bot.global_commands_get_sync()) {
                bot.global_command_delete(command_id);
            }
        }
    });

    bot.start(dpp::st_return);
    fut.wait();


}
