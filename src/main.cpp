//
// Created by thegreatleapforward on 03/01/23.
//
#include <dpp/dpp.h>
#include <iostream>
#include <set>
#include <ranges>

using namespace std::chrono_literals;
namespace ran = std::ranges;

/*class message_collector_t : dpp::message_collector {
public:
    message_collector_t(dpp::cluster* cl, dpp::cache<dpp::message>* c, std::vector<dpp::snowflake>* v, std::promise<void>&& p)
    : dpp::message_collector(cl, std::chrono::seconds::max().count()), cache{c}, id_vec{v}, promise{std::move(p)} {
        std::cout << "collector created!\n";
    }

private:
    const long time = std::chrono::duration_cast<std::chrono::seconds>((std::chrono::system_clock::now() - std::chrono::weeks{1}).time_since_epoch()).count();
    dpp::cache<dpp::message>* cache;
    std::vector<dpp::snowflake>* id_vec;
    std::promise<void> promise;

    void completed(const std::vector<dpp::message>& list) override {
        for (const auto& msg: list) {
            id_vec->emplace_back(msg.id);
            auto* to_save = new dpp::message{msg};
            cache->store(to_save);
        }

        promise.set_value();
    }

    const dpp::message* filter(const dpp::message_create_t& event) override {
        if (static_cast<long>(event.msg.id.get_creation_time()) < time) {
            return nullptr;
        }
        std::cout << event.msg.content << std::endl;
        return &event.msg;
    }
};*/

auto cur_msg_time() {
    return duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() << 22;
}

struct msgs_and_ids {
    explicit msgs_and_ids(::uint64_t id) : guild(id) {}

    dpp::snowflake guild;
    dpp::cache<dpp::message> messages;

    std::shared_mutex mi_mutex;
    std::set<dpp::snowflake> message_ids;

    void mt_insert(const dpp::message &msg) {
        auto lock = std::unique_lock(mi_mutex);

        if (this->message_ids.emplace(msg.id).second) {
            this->messages.store(new dpp::message{msg}); //move here has no benefit apparently
            //owner->log(dpp::ll_info, msg.content);
        }
    }

    template<class Rep, class Period>
    void gc(const std::chrono::duration<Rep, Period> &offset) {
        const auto time = (std::chrono::system_clock::now() - offset).time_since_epoch();
        auto num = std::chrono::duration_cast<std::chrono::seconds>(time).count() << 22;

        std::erase_if(this->message_ids, [this, &time](dpp::snowflake id) -> bool {
            //TODO this can bbe optimized by taking advantage of the fact that sets are sorted
            if (std::chrono::seconds{static_cast<long>(id.get_creation_time())} < time) {
                this->messages.remove(this->messages.find(id));
                return true;
            }
            return false;
        });
    }

    msgs_and_ids(msgs_and_ids&& rhs)  noexcept {
        this->guild = rhs.guild;
    }
};

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

    const std::array<dpp::snowflake, 1> guild_ids {820855382472785921};
    std::unordered_map<dpp::snowflake, msgs_and_ids> guild_info;
    for (auto id: guild_ids) {
        guild_info.insert({id, msgs_and_ids{id}});
    }

    bot.on_guild_create([&guild_ids, &bot, &guild_info](const dpp::guild_create_t& event){
        const auto id = event.created->id;
        if ( ran::find(guild_ids, id) == guild_ids.cend()) {
            bot.current_user_leave_guild(id);
        }
        else {
            auto channels = event.created->channels;
            ran::sort(channels);
            auto it = ran::unique(channels);
            channels.erase(it.begin(), it.end());

            const auto time = cur_msg_time();

            for (auto channel_id: channels ) {
                bot.messages_get(channel_id, 0, time, 0, 100,
                                 [&bot, &messages = guild_info.at(id), &channel_id](const auto &msg_event) {
                    if (msg_event.is_error()) {
                        bot.log(dpp::ll_error, msg_event.get_error().message);
                    }
                    else {
                        auto map = msg_event.template get<dpp::message_map>();
                        if (!map.size()) {
                            bot.log(dpp::ll_error, "No Messages Found for channel: " + std::to_string(channel_id));
                        }
                        for (const auto &[message_id, message]: map) {
                            messages.mt_insert(message);
                        }
                    }
                });
            }
        }
    });

    //probably don't need to actually cache the messages, just keep a running total
    bot.on_message_create([&guild_info](const dpp::message_create_t& event){
        guild_info.at(event.msg.guild_id).mt_insert(event.msg);
    });

    bot.on_slashcommand([](const dpp::slashcommand_t& event) {
        auto name = event.command.get_command_name();
        if (name == "ping") {
            event.reply("Pong!");
        }
        else if (name == "debug") {

        }
    });

    bot.on_ready([&bot, &guild_info](const dpp::ready_t& event) {
        if (dpp::run_once<struct register_bot_commands>()) {
            using sc = dpp::slashcommand;

            for (const auto& [command_id, command]: bot.global_commands_get_sync()) {
                bot.global_command_delete(command_id);
            }

            bot.global_bulk_command_create(std::vector{
                sc("ping", "Ping pong!", bot.me.id),
                sc("debug", "dec", bot.me.id)
            });

            //garbage collection
            bot.start_timer(dpp::timer_callback_t{[&guild_info](auto param){
                for (auto& [id, info]: guild_info) {
                    info.gc(std::chrono::weeks{1});
                }
            }}, duration_cast<std::chrono::seconds>(std::chrono::days{1}).count());
        }
    });

    bot.start(dpp::st_wait);
}
