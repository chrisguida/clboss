#undef NDEBUG
#include"Boss/Mod/ChannelFinderByPopularity.hpp"
#include"Boss/Mod/ConnectFinderByHardcode.hpp"
#include"Boss/Mod/Rpc.hpp"
#include"Boss/Mod/Waiter.hpp"
#include"Boss/Msg/Init.hpp"
#include"Boss/Msg/JsonCout.hpp"
#include"Boss/Msg/Option.hpp"
#include"Boss/Msg/ProposeConnectCandidates.hpp"
#include"Boss/Msg/SolicitConnectCandidates.hpp"
#include"Boss/Shutdown.hpp"
#include"Ev/Io.hpp"
#include"Ev/start.hpp"
#include"Jsmn/Object.hpp"
#include"Json/Out.hpp"
#include"Ln/NodeId.hpp"
#include"Net/Connector.hpp"
#include"Net/Fd.hpp"
#include"Net/SocketFd.hpp"
#include"S/Bus.hpp"
#include"Secp256k1/KeyPair.hpp"
#include"Secp256k1/PrivKey.hpp"
#include"Secp256k1/PubKey.hpp"
#include"Secp256k1/Signature.hpp"
#include"Secp256k1/SignerIF.hpp"
#include"Sha256/Hash.hpp"
#include"Sqlite3.hpp"
#include<array>
#include<assert.h>
#include<memory>
#include<set>
#include<sstream>
#include<stdexcept>
#include<string>
#include<sys/socket.h>
#include<vector>

/* On the "bitcoin" network, Msg::Init::blake2b chooses the hardcoded
 * peers and the default of clboss-min-nodes-to-process.  The peers
 * are observed by soliciting candidates until the draw has shown
 * several, the default through the log line that reverting to it
 * writes.  */

namespace {

class DummyConnector : public Net::Connector {
public:
	Net::SocketFd connect(std::string const&, int) override {
		return Net::SocketFd();
	}
};
class DummySigner : public Secp256k1::SignerIF {
public:
	Secp256k1::PubKey get_pubkey_tweak(Secp256k1::PrivKey const&) override {
		return Secp256k1::PubKey();
	}
	Secp256k1::Signature get_signature_tweak( Secp256k1::PrivKey const&
						, Sha256::Hash const&
						) override {
		return Secp256k1::Signature();
	}
	Sha256::Hash get_privkey_salted_hash(std::uint8_t salt[32]) override {
		auto hash = Sha256::Hash();
		if (salt)
			hash.from_buffer(salt);
		return hash;
	}
	Secp256k1::KeyPair
	get_keypair_tweak(Secp256k1::PrivKey const&) override {
		throw std::logic_error("Not expected to use.");
	}
};

struct Seen {
	std::set<std::string> peers;
	std::string default_log;
};

Seen run(bool blake2b) {
	auto bus = S::Bus();
	auto db = Sqlite3::Db(":memory:");
	auto sockets = std::array<int, 2>();
	auto res = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets.data());
	assert(res >= 0);
	auto server_end = Net::Fd(sockets[0]);
	auto rpc = Boss::Mod::Rpc(bus, Net::Fd(sockets[1]));
	auto connector = DummyConnector();
	auto signer = DummySigner();
	auto self_id = Ln::NodeId(std::string(
		"020000000000000000000000000000000000000000000000000000000000000000"
	));

	auto waiter = Boss::Mod::Waiter(bus);
	auto popularity = Boss::Mod::ChannelFinderByPopularity(bus, waiter);
	auto hardcode = Boss::Mod::ConnectFinderByHardcode(bus);

	auto seen = Seen();
	auto logs = std::vector<std::string>();
	bus.subscribe<Boss::Msg::JsonCout
		     >([&](Boss::Msg::JsonCout const& m) {
		auto is = std::istringstream(m.obj.output());
		auto js = Jsmn::Object();
		is >> js;
		if ( js.is_object() && js.has("method")
		  && std::string(js["method"]) == "log"
		   )
			logs.push_back(std::string(js["params"]["message"]));
		return Ev::lift();
	});
	bus.subscribe<Boss::Msg::ProposeConnectCandidates
		     >([&](Boss::Msg::ProposeConnectCandidates const& m) {
		for (auto const& c : m.candidates)
			seen.peers.insert(c);
		return Ev::lift();
	});

	auto code = Ev::lift().then([&]() {
		return bus.raise(Boss::Msg::Init{
			Boss::Msg::Network_Bitcoin, rpc, self_id, db,
			connector, signer, "", false,
			/* offline = */ false, blake2b
		});
	}).then([&]() {
		/* Two peers are drawn each time.  */
		auto act = Ev::lift();
		for (auto i = 0; i < 100; ++i)
			act += bus.raise(Boss::Msg::SolicitConnectCandidates{});
		return act;
	}).then([&]() {
		auto reason = std::make_shared<std::string>();
		return bus.raise(Boss::Msg::Option{
			"clboss-min-nodes-to-process",
			Jsmn::Object::parse_json("\"-1\""),
			reason
		}).then([reason]() {
			assert(reason->empty());
			return Ev::lift();
		});
	}).then([&]() {
		assert(!logs.empty());
		seen.default_log = logs.back();
		/* Stop the Rpc watchers so Ev::start can return.  */
		return bus.raise(Boss::Shutdown());
	}).then([&]() {
		return Ev::lift(0);
	});

	auto ec = Ev::start(code);
	assert(ec == 0);
	return seen;
}

}

int main() {
	auto sha256d = run(false);
	auto blake2b = run(true);

	/* Each has peers, and shares none with the other: a node
	 * of one chain refuses the peers of the other.  */
	assert(sha256d.peers.size() > 2);
	assert(blake2b.peers.size() > 2);
	for (auto const& p : blake2b.peers)
		assert(sha256d.peers.count(p) == 0);

	assert(sha256d.default_log.find("network default 800")
	       != std::string::npos);
	assert(blake2b.default_log.find("network default 20")
	       != std::string::npos);

	return 0;
}
