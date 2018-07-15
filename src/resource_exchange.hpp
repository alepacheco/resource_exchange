#pragma once
#include <eosiolib/currency.hpp>
#include <eosiolib/eosio.hpp>
#include <eosiolib/print.hpp>
#include <eosiolib/singleton.hpp>
#include <eosiolib/time.hpp>
#include <eosiolib/transaction.hpp>
#include <eosiolib/types.hpp>
namespace eosio {
class resource_exchange : public eosio::contract {
 private:
  account_name _contract;
  const uint32_t CYCLE_TIME = 60 * 60 * 25 * 3;  // 3 days and three hours
  const double PRICE_TUNE = 0.000001;
  const double PRICE_GAP = 0.9;

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
    asset to_be_refunding;
    asset refunding;

    asset get_total() const {
      return liquid_funds + total_stacked + to_be_refunding + refunding;
    }
    asset get_unstaked() const {
      return liquid_funds + to_be_refunding + refunding;
    }
    EOSLIB_SERIALIZE(state_t, (liquid_funds)(total_stacked)(timestamp)(
                                  to_be_refunding)(refunding))
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
  asset billaccount(account_name account, double cost_per_token);
  void matchbandwidth(account_name user);
  void payreward(account_name user, asset fee_collected);
  double cost_function(double total, double liquid, double TUNE);
  void unstakeunknown();

  void state_on_deposit(asset quantity);
  void state_on_withdraw(asset quantity);
  void state_set_timestamp(time_point_sec this_time);
  void state_on_sellstake(asset stake_from_account, asset stake_from_tx);
  void state_on_buystake(asset stake);
  void state_on_reset_account(asset account_res);
  void state_on_undelegate_unknown(asset delegated);
  void state_unstake_delayed(asset amount);
  void state_change(asset liquid, asset staked);
  void state_cycle();
  void state_init();

  void docycle();

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

  /// @abi action
  double calcosttoken();

  /// @abi action
  void cycle();
};
}  // namespace eosio
