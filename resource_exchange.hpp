#include <eosiolib/currency.hpp>
#include <eosiolib/eosio.hpp>
#include <eosiolib/singleton.hpp>
#include <eosiolib/time.hpp>
#include <eosiolib/types.hpp>

namespace eosio {
class resource_exchange : public eosio::contract {
 private:
  account_name _contract;

  struct stake_trade {
    account_name user;
    asset net;
    asset cpu;
  };

  struct withdraw_tx {
    account_name user;
    asset quantity;
  };

  //@abi table pendingtx i64
  struct pendingtx {
    pendingtx(account_name o = account_name()) : user(o) {}
    account_name user;
    asset net;
    asset cpu;
    asset get_all() const { return cpu + net; }
    bool is_empty() const { return !(net.amount | cpu.amount); }

    uint64_t primary_key() const { return user; }
    EOSLIB_SERIALIZE(pendingtx, (user)(net)(cpu))
  };

  //@abi table state i64
  struct state_t {
    asset liquid_funds;
    asset total_stacked;
    time_point_sec timestamp;

    asset get_total() const { return liquid_funds + total_stacked; }
    EOSLIB_SERIALIZE(state_t, (liquid_funds)(total_stacked)(timestamp))
  };

  //@abi table account i64
  struct account_t {
    account_t(account_name o = account_name()) : owner(o) {}
    account_name owner;
    asset balance = asset(0);
    asset resource_net = asset(0);
    asset resource_cpu = asset(0);
    asset get_all() const { return resource_cpu + resource_net; }

    bool is_empty() const {
      return !(balance.amount | resource_net.amount | resource_cpu.amount);
    }

    uint64_t primary_key() const { return owner; }
    EOSLIB_SERIALIZE(account_t, (owner)(balance)(resource_net)(resource_cpu))
  };

  struct delegated_bandwidth {
    account_name from;
    account_name to;
    asset net_weight;
    asset cpu_weight;
    uint64_t primary_key() const { return to; }
    EOSLIB_SERIALIZE(delegated_bandwidth, (from)(to)(net_weight)(cpu_weight))
  };

  typedef eosio::multi_index<N(delband), delegated_bandwidth>
      del_bandwidth_table;

  struct account_balance {
    asset balance;
    uint64_t primary_key() const { return balance.symbol.name(); }
  };

  typedef eosio::multi_index<N(accounts), account_balance> account_balances;

  typedef singleton<N(state), state_t> state_index;
  state_index contract_state;

  typedef eosio::multi_index<N(account), account_t> account_index;
  account_index accounts;

  typedef eosio::multi_index<N(pendingtx), pendingtx> pendingtx_index;
  pendingtx_index pendingtxs;

  void delegatebw(account_name receiver, asset stake_net_quantity,
                  asset stake_cpu_quantity);
  void undelegatebw(account_name receiver, asset stake_net_quantity,
                    asset stake_cpu_quantity);

  void dobuystake(account_name user, asset net, asset cpu);

  void reset_delayed_tx(pendingtx tx);
  void billaccount(account_name account, double cost_per_token);
  void matchbandwidth(account_name user);
  void payreward(account_name user, double cost_per_token);
  void unstakeunknown();

 public:
  resource_exchange(account_name self)
      : _contract(self),
        eosio::contract(self),
        accounts(_self, _self),
        pendingtxs(_self, _self),
        delegated_table(N(eosio), _self),
        contract_balance(N(eosio.token), _self),
        contract_state(_self, _self) {}

  del_bandwidth_table delegated_table;
  account_balances contract_balance;

  void apply(account_name contract, account_name act);
  void deposit(currency::transfer tx);

  /// @abi action
  void buystake(account_name user, asset net, asset cpu);

  /// @abi action
  void sellstake(account_name user, asset net, asset cpu);

  /// @abi action
  void withdraw(account_name user, asset quantity);

  asset calcost(asset res);
  double calcosttoken();

  /// @abi action
  void cycle();
};
}  // namespace eosio
