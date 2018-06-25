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

  int64_t calculate_cost(asset stake_net_quantity, asset stake_cpu_quantity);

 public:
  resource_exchange(account_name self)
      : _contract(self),
        eosio::contract(self),
        accounts(_self, _self),
        contract_state(_self, _self) {}

  //@abi table state i64
  struct state {
    asset liquid_funds;
    asset total_stacked;
    asset get_total() const { return liquid_funds + total_stacked; }
    EOSLIB_SERIALIZE(state, (liquid_funds)(total_stacked))
  };

  struct withdraw_tx {
    account_name to;
    asset quantity;
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

  typedef singleton<N(state), state> state_index;
  state_index contract_state;

  typedef eosio::multi_index<N(account), account> account_index;
  account_index accounts;

  void apply(account_name contract, account_name act);
  void deposit(currency::transfer tx);

  /// @abi action
  void buystake(account_name from, account_name to, asset net, asset cpu);

  /// @abi action
  void sellstake(account_name from, account_name to, asset net, asset cpu);

  /// @abi action
  void withdraw(account_name to, asset quantity);
};
}  // namespace eosio
