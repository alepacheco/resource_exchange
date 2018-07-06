#include "resource_exchange.hpp"
#include "accounts.cpp"
#include "bandwidth.cpp"
#include "pricing.cpp"
#include "stake.cpp"
#include "state_manager.cpp"

namespace eosio {
void resource_exchange::apply(account_name contract, account_name act) {
  state_init();

  switch (act) {
    case N(transfer): {
      eosio_assert(contract == N(eosio.token),
                   "invalid contract, use eosio.token");
      auto tx = unpack_action_data<currency::transfer>();
      if (tx.from != _contract) {
        require_auth(tx.from);
        deposit(tx);
      }
      break;
    }
    case N(withdraw): {
      auto tx = unpack_action_data<withdraw_tx>();
      require_auth(tx.user);
      withdraw(tx.user, tx.quantity);
      break;
    }
    case N(buystake): {
      auto tx = unpack_action_data<stake_trade>();
      require_auth(tx.user);
      buystake(tx.user, tx.net, tx.cpu);
      break;
    }
    case N(sellstake): {
      auto tx = unpack_action_data<stake_trade>();
      require_auth(tx.user);
      sellstake(tx.user, tx.net, tx.cpu);
      break;
    }
    case N(cycle): {
      require_auth(_contract);
      cycle();
      break;
    }
  }
}

/**
 * Cycle is run every CYCLE_TIME
 **/
void resource_exchange::cycle() {
  print("Run cycle\n");
  auto secs_to_next = time_point_sec(CYCLE_TIME);
  auto secs_flexibility = time_point_sec(5);
  auto state = contract_state.get();
  time_point_sec this_time = time_point_sec(now());
  eosio::transaction out;
  out.actions.emplace_back(permission_level(_contract, N(active)), _contract,
                           N(cycle), _contract);

  if ((state.timestamp.utc_seconds + secs_to_next.utc_seconds) >
      this_time.utc_seconds) {
    out.delay_sec = (state.timestamp.utc_seconds + secs_to_next.utc_seconds) -
                    (this_time.utc_seconds + secs_flexibility.utc_seconds);
    out.send(this_time.utc_seconds, _contract);
    return;
  }

  docycle();

  out.delay_sec = secs_to_next.utc_seconds;
  out.send(this_time.utc_seconds, _contract);

  state_set_timestamp(this_time);
}

void resource_exchange::docycle() {
  // TODO proccess withdraws first
  double cost_per_token = calcosttoken();
  asset fees_collected = asset(0);
  for (auto acnt = accounts.begin(); acnt != accounts.end(); ++acnt) {
    fees_collected += billaccount(acnt->owner, cost_per_token);
    matchbandwidth(acnt->owner);
  }
  for (auto acnt = accounts.begin(); acnt != accounts.end(); ++acnt) {
    payreward(acnt->owner, fees_collected);
  }
  unstakeunknown();

  print("Total fees: ", fees_collected, " ");
}

asset resource_exchange::billaccount(account_name owner,
                                     double cost_per_token) {
  auto acnt = accounts.find(owner);
  auto pending_itr = pendingtxs.find(acnt->owner);

  auto cost_all = asset(cost_per_token * acnt->get_all().amount);
  asset extra_net = asset(0);
  asset extra_cpu = asset(0);
  asset fee_collected = asset(0);

  if (pending_itr != pendingtxs.end()) {
    cost_all += asset(cost_per_token * pending_itr->get_all().amount);
    extra_net += pending_itr->net;
    extra_cpu += pending_itr->cpu;
  }

  eosio_assert(cost_all.amount >= 0, "cost negative");
  if (acnt->balance >= cost_all) {
    accounts.modify(acnt, 0, [&](auto& account) {
      account.balance -= cost_all;
      account.resource_net += extra_net;
      account.resource_cpu += extra_cpu;
    });
    fee_collected += cost_all;
  } else {
    if (pending_itr != pendingtxs.end()) {
      reset_delayed_tx(*pending_itr);
    }
    asset cost_account = asset(cost_per_token * acnt->get_all().amount);
    if (acnt->balance >= cost_account) {
      accounts.modify(acnt, 0,
                      [&](auto& account) { account.balance -= cost_account; });
      fee_collected += cost_account;
    } else {
      // no balance: reset account
      state_on_reset_account(acnt->resource_net + acnt->resource_cpu);
      accounts.modify(acnt, 0, [&](auto& account) {
        account.resource_net = asset(0);
        account.resource_cpu = asset(0);
      });
    }
  }
  if (pending_itr != pendingtxs.end()) {
    pendingtxs.erase(pending_itr);
  }
  return fee_collected;
}

void resource_exchange::matchbandwidth(account_name owner) {
  auto user = accounts.find(owner);
  auto delegated = delegated_table.find(user->owner);

  asset net_delegated = asset(0);
  asset cpu_delegated = asset(0);

  if (delegated != delegated_table.end()) {
    net_delegated = delegated->net_weight;
    cpu_delegated = delegated->cpu_weight;
  }
  asset net_account = user->resource_net;
  asset cpu_account = user->resource_cpu;
  asset net_to_delegate = asset(0);
  asset net_to_undelegate = asset(0);
  asset cpu_to_delegate = asset(0);
  asset cpu_to_undelegate = asset(0);
  if (net_account > net_delegated) {
    net_to_delegate += (net_account - net_delegated);
  } else if (net_account < net_delegated) {
    net_to_undelegate += (net_delegated - net_account);
  }

  if (cpu_account > net_delegated) {
    cpu_to_delegate += (cpu_account - cpu_delegated);
  } else if (net_account < net_delegated) {
    cpu_to_undelegate += (cpu_delegated - cpu_account);
  }
  if ((net_to_delegate + cpu_to_delegate) > asset(0)) {
    delegatebw(user->owner, net_to_delegate, cpu_to_delegate);
  }
  if ((net_to_undelegate + cpu_to_undelegate) > asset(0)) {
    undelegatebw(user->owner, net_to_undelegate, cpu_to_undelegate);
    // TODO add to refunding
    // * Remove from price and available calculation
    // * Set as liquid on next cycle
  }
}

}  // namespace eosio

extern "C" {
[[noreturn]] void apply(uint64_t receiver, uint64_t code, uint64_t action) {
  eosio::resource_exchange ex(receiver);
  ex.apply(code, action);
  eosio_exit(0);
}
}