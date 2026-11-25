#include"Boss/Mod/BoltzSwapper/Env.hpp"
#include"Boss/Mod/BoltzSwapper/Main.hpp"
#include"Boss/Mod/BoltzSwapper/ServiceCreator.hpp"
#include"Util/make_unique.hpp"

namespace {

    auto const boltz_instances = std::map< Boss::Msg::Network, std::vector<Boss::Mod::BoltzSwapper::Instance> >
    {
        { Boss::Msg::Network_Bitcoin,
    /* Historically, the clearnet API endpoint was used as the in-database label
     * for Boltz services.
     * The boltz.exchange service thus uses the API endpoint as the label for
     * back-compatibility.
     * Other services since then were added after we had a separate label.
     * The URLs are API roots: the service appends the /v2/... paths.
     */
      {
          { "https://boltz.exchange/api",
          "https://api.boltz.exchange",
          "http://boltzzzbnus4m7mta3cxmflnps4fp7dueu2tgurstbvrbt6xswzcocyd.onion/api"
          },
          { "AutonomousOrganization@github.com",
              "",
              "http://jsyqqszgfrya6nj7nhi4hu4tdpuvfursl7dyxeiukzit5mvckqbzxpad.onion"
            },
          /* The services below swap on the BLAKE2b chain, where
           * lightningd also reports the network as
           * "bitcoin"; a node is offered them, and not the ones
           * above, when it advertises option_blake2b.
           * All run paulscode/lightning-fork-swap or a fork of it,
           * which serves the Boltz API v2 at the site root.
           */
          { "plebswap.com",
            "https://plebswap.com",
            "",
            /* blake2b = */ true
          },
          { "lightningfork.com",
            "https://lightningfork.com",
            "",
            /* blake2b = */ true
          },
          { "swap.kilombino.com",
            "https://swap.kilombino.com",
            "http://swapkqsnxocu2rnmwtaswpvchgl3teooeqgx43ookk5qqhzyx4rzqhid.onion",
            /* blake2b = */ true
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
        { Boss::Msg::Network_Regtest,
    /* A regtest chain is one's own, and so is its Boltz backend:
     * 9001 is the port the backend's API listens on by default.
     */
            {
              { "regtest-boltz-backend-for-clboss",
                  "http://127.0.0.1:9001",
                  ""
              },
              /* The same, for a regtest chain with the BLAKE2b
               * rules: the backend is whichever one runs there.  */
              { "regtest-boltz-backend-for-clboss",
                  "http://127.0.0.1:9001",
                  "",
                  /* blake2b = */ true
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
