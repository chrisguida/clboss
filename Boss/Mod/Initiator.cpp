#include"Boss/Mod/Initiator.hpp"
#include"Boss/Mod/Rpc.hpp"
#include"Boss/Msg/CommandRequest.hpp"
#include"Boss/Msg/CommandResponse.hpp"
#include"Boss/Msg/Init.hpp"
#include"Boss/log.hpp"
#include"Ev/ThreadPool.hpp"
#include"Jsmn/Object.hpp"
#include"Json/Out.hpp"
#include"Ln/NodeId.hpp"
#include"Net/Connector.hpp"
#include"Net/DirectConnector.hpp"
#include"Net/Fd.hpp"
#include"Net/ProxyConnector.hpp"
#include"S/Bus.hpp"
#include"Sqlite3.hpp"
#include"Util/make_unique.hpp"
#include<algorithm>
#include<assert.h>
#include<sstream>
#include<stdlib.h>

namespace {

std::string stringify_jsmn(Jsmn::Object const& js) {
	auto os = std::ostringstream();
	os << js;
	return os.str();
}

}

namespace Boss { namespace Mod {

class Initiator::Impl {
private:
	S::Bus& bus;
	Ev::ThreadPool& threadpool;
	std::function<Net::Fd( std::string const&
			     , std::string const&
			     )> open_rpc_socket;

	Sqlite3::Db db;

	Ln::NodeId self_id;

	bool initted;
	std::uint64_t init_id;
	Boss::Msg::Network network;
	std::unique_ptr<Boss::Mod::Rpc> rpc;

	std::unique_ptr<Net::Connector> connector;

	Ev::Io<void> error( std::string const& comment
			  , Jsmn::Object const& params
			  ) {
		auto ps = stringify_jsmn(params);
		return Boss::log( bus, Boss::Error
				, "init: %s: %s"
				, comment.c_str()
				, ps.c_str()
				).then([]() {
			abort();
			return Ev::lift();
		});
	}
	Ev::Io<void> invalid_result(char const* meth, Jsmn::Object info) {
		auto is = stringify_jsmn(info);
		return Boss::log( bus, Boss::Error
				, "Unexpected %s result: %s"
				, meth
				, is.c_str()
				).then([]() {
			throw std::runtime_error("Unexpected result.");
			return Ev::lift();
		});
	}

	void setup_proxy(std::string proxy) {
		auto host = std::string();
		auto port = int();
		auto rit = std::find( proxy.rbegin(), proxy.rend()
				   , ':'
				   );
		if (rit == proxy.rend()) {
			host = proxy;
			port = 9050;
		} else {
			auto it = rit.base();
			host = std::string(proxy.begin(), it - 1);
			auto port_s = std::string(it, proxy.end());
			auto is = std::istringstream(port_s);
			is >> port;
		}
		connector = Util::make_unique<Net::ProxyConnector>(
			std::move(connector), host, port
		);
	}

public:
	Impl( S::Bus& bus_
	    , Ev::ThreadPool& threadpool_
	    , std::function<Net::Fd( std::string const&
				   , std::string const&
				   )> open_rpc_socket_
	    ) : bus(bus_)
	      , threadpool(threadpool_)
	      , open_rpc_socket(std::move(open_rpc_socket_))
	      , db()
	      , initted(false)
	      {
		assert(open_rpc_socket);

		bus.subscribe<Boss::Msg::CommandRequest>([this](Boss::Msg::CommandRequest const& c) {
			if (c.command != "init")
				return Ev::lift();

			init_id = c.id;
			auto const& params = c.params;

			/* A lot of the mess here is testing that the
			 * lightningd is properly working... */

			if (initted)
				return error("multiple init", params);
			initted = true;

			if (!params.is_object())
				return error("params not object", params);
			if (!params.has("configuration"))
				return error( "no 'configuration' param"
					    , params
					    );
			auto configuration = params["configuration"];
			if (!configuration.is_object())
				return error( "configuration not object"
					    , configuration
					    );
			if (!configuration.has("network"))
				return error( "no 'network' configuration"
					    , configuration
					    );
			auto network_js = configuration["network"];
			if (!network_js.is_string())
				return error( "network not string"
					    , network_js
					    );
			auto network_s = std::string(network_js);
			if (network_s == "bitcoin")
				network = Boss::Msg::Network_Bitcoin;
			else if (network_s == "testnet")
				network = Boss::Msg::Network_Testnet;
			else if (network_s == "regtest")
				network = Boss::Msg::Network_Regtest;
			else
				return error( "unrecognized network"
					    , network_js
					    );

			auto lightning_dir = std::string(".");
			if (configuration.has("lightning-dir")) {
				auto lightning_dir_js = configuration["lightning-dir"];
				if (!lightning_dir_js.is_string())
					return error( "lightning-dir not string"
						    , lightning_dir_js
						    );
				lightning_dir = std::string(lightning_dir_js);
			}

			auto rpc_file = std::string("lightning-rpc");
			if (configuration.has("rpc-file")) {
				auto rpc_file_js = configuration["rpc-file"];
				if (!rpc_file_js.is_string())
					return error( "rpc-file not string"
						    , rpc_file_js
						    );
				rpc_file = std::string(rpc_file_js);
			}

			/* Now construct the RPC socket.  */
			return threadpool.background< Net::Fd
						    >([ this
						      , lightning_dir
						      , rpc_file
						      ]() {
				return open_rpc_socket( lightning_dir
						      , rpc_file
						      );
			}).then([this](Net::Fd fd) {
				rpc = Util::make_unique<Boss::Mod::Rpc>
					(bus, std::move(fd));
				return Boss::log( bus, Debug
						, "RPC socket opened."
						);
			}).then([this]() {
				db = Sqlite3::Db("data.clboss");
				return db.transact();
			}).then([this](Sqlite3::Tx tx) {
				tx.query_execute("PRAGMA application_id = 0x424F5353;");
				tx.query_execute("PRAGMA user_version = 0x2020434C;");
				tx.commit();
				return Boss::log( bus, Debug
						, "Database file opened."
						);
			}).then([this]() {
				return rpc->command( "getinfo"
						   , Json::Out::empty_object()
						   );
			}).then([this](Jsmn::Object info) {
				auto invalid_getinfo = [this](Jsmn::Object r) {
					return invalid_result( "getinfo"
							     , std::move(r)
							     );
				};

				if (!info.is_object() || !info.has("id"))
					return invalid_getinfo(
						std::move(info)
					);
				auto id = info["id"];
				if (!id.is_string())
					return invalid_getinfo(
						std::move(info)
					);
				auto s_id = std::string(id);
				if (!Ln::NodeId::valid_string(s_id))
					return invalid_getinfo(
						std::move(info)
					);

				self_id = Ln::NodeId(s_id);

				connector = Util::make_unique<
					Net::DirectConnector
				>();

				return Ev::lift();
			}).then([this]() {
				return rpc->command( "listconfigs"
						   , Json::Out::empty_object()
						   );
			}).then([this](Jsmn::Object cfg) {
				auto invalid_cfg = [this](Jsmn::Object r) {
					return invalid_result( "listconfigs"
							     , std::move(r)
							     );
				};
				if (!cfg.is_object())
					return invalid_cfg(std::move(cfg));
				if ( cfg.has("proxy")
				  && cfg["proxy"].is_string()
				   ) {
					auto proxy = std::string(
						cfg["proxy"]
					);
					setup_proxy(proxy);
				}

				return bus.raise(Boss::Msg::Init{
					network, *rpc, self_id, db,
					*connector
				});
			}).then([this]() {
				return Boss::log( bus, Debug
						, "Initialization raised."
						);
			}).then([this]() {
				/* Now respond.  */
				return bus.raise(Boss::Msg::CommandResponse{
					init_id,
					Json::Out::empty_object()
				});
			}).then([this]() {
				return Boss::log( bus, Info
						, "Started."
						);
			});
		});
	}
};

Initiator::Initiator( S::Bus& bus
		    , Ev::ThreadPool& threadpool
		    , std::function<Net::Fd( std::string const&
					   , std::string const&
					   )> open_rpc_socket
		    ) : pimpl(Util::make_unique<Impl>( bus, threadpool
						     , std::move(open_rpc_socket)
						     ))
		      { }

Initiator::Initiator(Initiator&& o) : pimpl(std::move(o.pimpl)) { }
Initiator::~Initiator() { }

}}