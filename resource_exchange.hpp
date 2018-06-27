#include <eosiolib/currency.hpp>
#include <eosiolib/eosio.hpp>
#include <eosiolib/singleton.hpp>
#include <eosiolib/types.hpp>

namespace eosio {
class resource_exchange : public eosio::contract {
 private:
  account_name _contract;

  void delegatebw(account_name receiver, asset stake_net_quantity,
                  asset stake_cpu_quantity);
  void undelegatebw(account_name receiver, asset stake_net_quantity,
                    asset stake_cpu_quantity);

  void dobuystake(account_name user, asset net, asset cpu);

 public:
  resource_exchange(account_name self)
      : _contract(self),
        eosio::contract(self),
        accounts(_self, _self),
        pendingtxs(_self, _self),
        contract_state(_self, _self) {}

  struct withdraw_tx {
    account_name to;
    asset quantity;
  };

  //@abi table state i64
  struct state_t {
    asset liquid_funds;
    asset total_stacked;
    asset get_total() const { return liquid_funds + total_stacked; }
    EOSLIB_SERIALIZE(state_t, (liquid_funds)(total_stacked))
  };

  struct stake_trade {
    account_name from;
    account_name to;
    asset net;
    asset cpu;
  };

  //@abi table account i64
  struct account {
    account(account_name o = account_name()) : owner(o) {}
    account_name owner;
    asset balance = asset(0);
    asset resource_net = asset(0);
    asset resource_cpu = asset(0);

    bool is_empty() const {
      return !(balance.amount | resource_net.amount | resource_cpu.amount);
    }

    uint64_t primary_key() const { return owner; }
    EOSLIB_SERIALIZE(account, (owner)(balance)(resource_net)(resource_cpu))
  };

  //@abi table pendingtx i64
  struct pendingtx {
    pendingtx(account_name o = account_name()) : user(o) {}
    account_name user;
    asset net;
    asset cpu;
    asset withdrawing;  // positive mumber, but withdrawing

    uint64_t primary_key() const { return user; }
    EOSLIB_SERIALIZE(pendingtx, (user)(net)(cpu)(withdrawing))
  };

  typedef singleton<N(state_t), state_t> state_index;
  state_index contract_state;

  typedef eosio::multi_index<N(account), account> account_index;
  account_index accounts;

  typedef eosio::multi_index<N(pendingtx), pendingtx> pendingtx_index;
  pendingtx_index pendingtxs;

  void apply(account_name contract, account_name act);
  void deposit(currency::transfer tx);

  /// @abi action
  void buystake(account_name user, asset net, asset cpu);

  /// @abi action
  void sellstake(account_name user, asset net, asset cpu);

  /// @abi action
  void withdraw(account_name user, asset quantity);

  /// @abi action
  asset calcost(asset res);

  /// @abi action
  void cycle();
};
}  // namespace eosio
