#include "resource_exchange.hpp"
#include "bandwidth.cpp"
#include "accounts.cpp"
#include "stake.cpp"

namespace eosio {
void resource_exchange::apply(account_name contract, account_name act) {
  if (!contract_state.exists()) {
    contract_state.set(state_t{asset(0), asset(0), time_point_sec(0)},
                       _contract);
  }

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
 * Reset_delayed_tx will revert the state when a delayed tx cannot be completed
 **/
void resource_exchange::reset_delayed_tx(pendingtx tx) {
  auto state = contract_state.get();
  contract_state.set(
      state_t{state.liquid_funds + asset(tx.net.amount + tx.cpu.amount),
              state.total_stacked - asset(tx.net.amount + tx.cpu.amount),
              state.timestamp},
      _self);
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

  // set timestamp
  contract_state.set(
      state_t{state.liquid_funds, state.total_stacked, this_time}, _self);
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

asset resource_exchange::billaccount(account_name owner, double cost_per_token) {
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
      auto state = contract_state.get();
      contract_state.set(
          state_t{
              state.liquid_funds + (acnt->resource_net + acnt->resource_cpu),
              state.total_stacked - (acnt->resource_net + acnt->resource_cpu),
              state.timestamp},
          _self);
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

void resource_exchange::payreward(account_name user, asset fee_collected) {
  auto state = contract_state.get();
  double fees = fee_collected.amount * 1;
  double reward_per_token = fees/state.get_total().amount;
  auto acnt = accounts.find(user);
  double reward = acnt->balance.amount * reward_per_token;
  accounts.modify(acnt, 0,
                  [&](auto& account) { account.balance += asset(reward); });
}

void resource_exchange::unstakeunknown() {
  if (delegated_table.begin() == delegated_table.end()) {
    return;
  }
  for (auto delegated = delegated_table.begin();
       delegated != delegated_table.end(); ++delegated) {
    if (accounts.find(delegated->to) == accounts.end() &&
        delegated->to != _contract) {
      undelegatebw(delegated->to, delegated->net_weight, delegated->cpu_weight);
      auto state = contract_state.get();
      contract_state.set(state_t{state.liquid_funds + (delegated->net_weight +
                                                       delegated->cpu_weight),
                                 state.total_stacked - (delegated->net_weight +
                                                        delegated->cpu_weight),
                                 state.timestamp},
                         _self);
    }
  }
}
/**
 * Function to calculate aproximate cost of resources taking into account state
 * changes with purchase
 **/
asset resource_exchange::calcost(asset resources) {
  if (resources <= asset(0)) {
    return asset(0);
  }
  eosio_assert(contract_state.exists(), "No contract state available");
  int PURCHASE_STEP = 10000;  // the lower the more precise but more cpu
  int PRICE_TUNE = 10000;
  auto state = contract_state.get();
  int64_t purchase = resources.amount;
  double_t liquid = state.liquid_funds.amount;
  double_t total = state.get_total().amount;

  // purchases of 1 EOS at a time, to get fair price as the resources get
  // scarcer
  int32_t steps = purchase / PURCHASE_STEP;
  double_t cost_per_token = 0;
  for (int i = 0; i < steps; i++) {
    cost_per_token += ((total * total / liquid) /
                       (total * PRICE_TUNE));  // TODO find optimal function
    liquid -= PURCHASE_STEP;
  }
  cost_per_token = cost_per_token / steps;

  asset price = asset(cost_per_token * purchase);
  print("price: ", price, "\n");
  return price;
}

/**
 * Returns cost per Larimer
**/
double resource_exchange::calcosttoken() {
  eosio_assert(contract_state.exists(), "No contract state available");
  int PRICE_TUNE = 10000;
  auto state = contract_state.get();
  double liquid = state.liquid_funds.amount;
  double total = state.get_total().amount;
  double cost_per_token = ((total * total / liquid) /
                           (total * PRICE_TUNE));  // TODO find optimal function

  print("price token: ", asset(cost_per_token * 10000), "::", cost_per_token, "\n");
  return cost_per_token;
}
}  // namespace eosio

extern "C" {
[[noreturn]] void apply(uint64_t receiver, uint64_t code, uint64_t action) {
  eosio::resource_exchange ex(receiver);
  ex.apply(code, action);
  eosio_exit(0);
}
}