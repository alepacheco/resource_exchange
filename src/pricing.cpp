#pragma once
#include "resource_exchange.hpp"

namespace eosio {
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
  auto state = contract_state.get();
  double liquid = state.liquid_funds.amount;
  double total = state.get_total().amount;
  double cost_per_token = ((total * total / liquid) /
                           (total * PRICE_TUNE));  // TODO find optimal function

  print("price token: ", asset(cost_per_token * 10000), "::", cost_per_token,
        "\n");
  return cost_per_token;
}

void resource_exchange::payreward(account_name user, asset fee_collected) {
  const double FEE_SHARE = 1;
  auto state = contract_state.get();
  double fees = fee_collected.amount * FEE_SHARE;
  double fees_devs = fee_collected.amount - fees;
  double reward_per_token = fees / state.get_total().amount;
  auto acnt = accounts.find(user);
  double reward = acnt->balance.amount * reward_per_token;
  accounts.modify(acnt, 0,
                  [&](auto& account) { account.balance += asset(reward); });

  if (fees_devs > 0) {
    // pay devs
    // remove from liquid
  }
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
    // Cancel purchase stake tx, pay just account
    if (pending_itr != pendingtxs.end()) {
      reset_delayed_tx(*pending_itr);
    }
    asset cost_account = asset(cost_per_token * acnt->get_all().amount);
    if (acnt->balance >= cost_account) {
      accounts.modify(acnt, 0,
                      [&](auto& account) { account.balance -= cost_account; });
      fee_collected += cost_account;
    } else {
      // can't pay for account, reset account
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

}  // namespace eosio