#include"Boss/Mod/BoltzSwapper/Env.hpp"
#include"Boss/Mod/BoltzSwapper/Main.hpp"
#include"Boss/Mod/BoltzSwapper/ServiceCreator.hpp"
#include"Util/make_unique.hpp"

namespace {

    auto const boltz_instances = std::map< Boss::Msg::Network, std::vector<Boss::Mod::BoltzSwapper::Instance> >
    {
        { Boss::Msg::Network_Bitcoin,
    /* On mainnet this build is for the BLAKE2b chain, whose CLN
     * still reports the network as "bitcoin".
     * boltz.exchange and the other services of the SHA256d chain
     * would lock up coins on a chain this node does not follow, so
     * they are not listed.
     * These two run paulscode/lightning-fork-swap, which serves the
     * Boltz API v2 at the site root: the service appends /v2/...
     * The label is the key in the database.
     */
      {
          { "plebswap.com",
            "https://plebswap.com",
            ""
          },
          { "lightningfork.com",
            "https://lightningfork.com",
            ""
          }
      }
    },
        { Boss::Msg::Network_Signet,
            {
              { "signet-boltz-backend-for-clboss",
                  "http://127.0.0.1:8080",
                  "http://boltz7ckqss7j66wjjqlm334qccsrjie552gdnvn6vwztnzk7bqwsdad.onion"
              }
          }
        },
        { Boss::Msg::Network_Testnet,
            {
              { "https://testnet.boltz.exchange/api",
                  "https://api.testnet.boltz.exchange",
                  "http://tboltzzrsoc3npe6sydcrh37mtnfhnbrilqi45nao6cgc6dr7n2eo3id.onion/api"
              }
          }
        }
    };

}

namespace Boss { namespace Mod { namespace BoltzSwapper {

    class Main::Impl {
    private:
        Env env;
        ServiceCreator creator;

    public:
        Impl(S::Bus& bus, Ev::ThreadPool& threadpool)
            : env(bus)
            , creator(bus, threadpool, env, boltz_instances)
        { }
    };

    Main::Main(Main&&) =default;
    Main& Main::operator=(Main&&) =default;
    Main::~Main() =default;

    Main::Main( S::Bus& bus
          , Ev::ThreadPool& threadpool
          ) : pimpl(Util::make_unique<Impl>(bus, threadpool))
    { }

}}}
