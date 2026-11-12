#undef NDEBUG
#include"Bitcoin/Tx.hpp"
#include"Boltz/EnvIF.hpp"
#include"Boss/Mod/BoltzSwapper/ServiceCreator.hpp"
#include"Boss/Mod/Rpc.hpp"
#include"Boss/Msg/Init.hpp"
#include"Boss/Msg/Network.hpp"
#include"Boss/Msg/Option.hpp"
#include"Boss/Msg/ProvideStatus.hpp"
#include"Boss/Msg/SolicitStatus.hpp"
#include"Boss/Shutdown.hpp"
#include"Ev/Io.hpp"
#include"Ev/ThreadPool.hpp"
#include"Ev/start.hpp"
#include"Ev/yield.hpp"
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
#include"Sha256/fun.hpp"
#include"Sqlite3.hpp"
#include<array>
#include<assert.h>
#include<cstdint>
#include<map>
#include<set>
#include<memory>
#include<sstream>
#include<string>
#include<sys/socket.h>
#include<vector>

/* The built-in swap services are chosen by the chain the node is
 * on, as Msg::Init::blake2b tells it; the services the operator lists
 * with `clboss-swap-providers` are taken as they are, on either chain.
 * The services that run are read through `clboss-status`.  */

namespace {

class MockEnv : public Boltz::EnvIF {
public:
	Ev::Io<std::uint32_t> get_feerate() override {
		return Ev::lift(std::uint32_t(1000));
	}
	Ev::Io<std::string> get_block(std::uint32_t) override {
		return Ev::lift(std::string());
	}
	Ev::Io<bool> has_confirmed_output(Bitcoin::TxId, std::uint32_t) override {
		return Ev::lift(false);
	}
	Ev::Io<bool> broadcast_tx(Bitcoin::Tx) override {
		return Ev::lift(true);
	}
	Ev::Io<void> logd(std::string) override { return Ev::lift(); }
	Ev::Io<void> logi(std::string) override { return Ev::lift(); }
	Ev::Io<void> loge(std::string) override { return Ev::lift(); }
};

class MockSigner : public Secp256k1::SignerIF {
private:
	Secp256k1::PrivKey privkey;

public:
	MockSigner()
		: privkey(Secp256k1::PrivKey(std::string(
			"0101010101010101010101010101010101010101010101010101010101010101"
		  )))
		{ }

	Secp256k1::PubKey get_pubkey_tweak(Secp256k1::PrivKey const&) override {
		return Secp256k1::PubKey(privkey);
	}
	Secp256k1::Signature get_signature_tweak( Secp256k1::PrivKey const&
						, Sha256::Hash const& m
						) override {
		return Secp256k1::Signature::create(privkey, m);
	}
	Sha256::Hash get_privkey_salted_hash(std::uint8_t salt[32]) override {
		return Sha256::fun(salt, 32);
	}
	Secp256k1::KeyPair
	get_keypair_tweak(Secp256k1::PrivKey const&) override {
		return Secp256k1::KeyPair(privkey);
	}
};

class DummyConnector : public Net::Connector {
public:
	Net::SocketFd connect(std::string const&, int) override {
		return Net::SocketFd();
	}
};

typedef Boss::Mod::BoltzSwapper::Instance Instance;
auto const instances = std::map<Boss::Msg::Network, std::vector<Instance>>
{ { Boss::Msg::Network_Regtest
  , { { "sha256d service", "https://sha256d.example.org", "" }
    , { "blake2b service", "https://blake2b.example.org", ""
      , /* blake2b = */ true
      }
    }
  }
};

Ev::Io<void> yields(std::size_t n) {
	auto act = Ev::yield();
	for (auto i = std::size_t(0); i < n; ++i)
		act = std::move(act).then([]() { return Ev::yield(); });
	return act;
}

/* The labels of the services that run.  `listed` is the value of
 * `clboss-swap-providers`, or null to leave it unset.  */
std::set<std::string> services(bool blake2b, char const* listed) {
	auto bus = S::Bus();
	auto threadpool = Ev::ThreadPool();
	auto env = MockEnv();
	auto signer = MockSigner();
	auto connector = DummyConnector();
	auto db = Sqlite3::Db(":memory:");
	auto sockets = std::array<int, 2>();
	auto res = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets.data());
	assert(res >= 0);
	auto server_end = Net::Fd(sockets[0]);
	auto rpc = Boss::Mod::Rpc(bus, Net::Fd(sockets[1]));
	auto self_id = Ln::NodeId(std::string(
		"020000000000000000000000000000000000000000000000000000000000000000"
	));

	auto creator = Boss::Mod::BoltzSwapper::ServiceCreator
		(bus, threadpool, env, instances);

	auto labels = std::set<std::string>();
	bus.subscribe<Boss::Msg::ProvideStatus
		     >([&labels](Boss::Msg::ProvideStatus const& m) {
		if (m.key != "boltz_swaps")
			return Ev::lift();
		auto is = std::istringstream(m.value.output());
		auto js = Jsmn::Object();
		is >> js;
		assert(js.is_object());
		for (auto const& label : js.keys())
			labels.insert(label);
		return Ev::lift();
	});

	auto code = Ev::lift().then([&]() {
		if (!listed)
			return Ev::lift();
		auto json = "\"" + std::string(listed) + "\"";
		return bus.raise(Boss::Msg::Option{
			"clboss-swap-providers",
			Jsmn::Object::parse_json(json.c_str()),
			nullptr
		});
	}).then([&]() {
		return bus.raise(Boss::Msg::Init{
			Boss::Msg::Network_Regtest, rpc, self_id, db,
			connector, signer, "", false,
			/* offline = */ false, blake2b
		});
	}).then([&]() {
		/* The services are created in the background.  */
		return yields(2000);
	}).then([&]() {
		return bus.raise(Boss::Msg::SolicitStatus{});
	}).then([&]() {
		return yields(200);
	}).then([&]() {
		/* Stop the Rpc watchers so Ev::start can return.  */
		return bus.raise(Boss::Shutdown());
	}).then([&]() {
		return Ev::lift(0);
	});

	auto ec = Ev::start(code);
	assert(ec == 0);
	return labels;
}

}

int main() {
	typedef std::set<std::string> Labels;

	assert(services(false, nullptr) == Labels{"sha256d service"});
	assert(services(true, nullptr) == Labels{"blake2b service"});

	/* A listed service is not asked which chain it is for.  */
	assert(services(false, "https://listed.example.com")
	       == Labels{"https://listed.example.com"});
	assert(services(true, "https://listed.example.com")
	       == Labels{"https://listed.example.com"});

	/* A built-in service of the other chain, named by its root,
	 * is not the built-in instance to this node: it is a listed
	 * service like any other.  */
	assert(services(true, "https://sha256d.example.org")
	       == Labels{"https://sha256d.example.org"});
	assert(services(true, "https://blake2b.example.org")
	       == Labels{"blake2b service"});

	return 0;
}
