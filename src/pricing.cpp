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
  int PURCHASE_STEP = 1;  // the lower the more precise but more cpu
  auto state = contract_state.get();
  int32_t steps = 100;

  int64_t purchase = resources.amount;
  double_t liquid = state.liquid_funds.amount;
  double_t total = state.get_total().amount;

  int32_t purchase_per_step = purchase / steps;
  double_t cost_per_token = 0;
  for (int i = 0; i < steps; i++) {
    cost_per_token += cost_function(total, liquid, PRICE_TUNE);
    liquid -= purchase_per_step;
  }
  cost_per_token = cost_per_token / steps;
  asset price = asset(cost_per_token * purchase);

  print("price: ", price);
  return price;
}

/**
 * Returns cost per Larimer
 **/
double resource_exchange::calcosttoken() {
  eosio_assert(contract_state.exists(), "No contract state available");
  auto state = contract_state.get();
  double larimer_per_token = 10000;
  double liquid = state.liquid_funds.amount;
  double total = state.get_total().amount;
  eosio_assert(liquid > 0 && total > 0, "No funds to price");
  double cost_per_token = cost_function(total, liquid, PRICE_TUNE);
  return cost_per_token;
}

double resource_exchange::cost_function(double total, double liquid, double TUNE) {
  double used = total - liquid;
  double price = 1.0 / (-used + (total * PRICE_GAP));
  return price / TUNE;
}

void resource_exchange::payreward(account_name user, asset fee_collected) {
  auto state = contract_state.get();
  double reward_per_token = fee_collected.amount / state.get_total().amount;
  auto acnt = accounts.find(user);
  double reward = acnt->balance.amount * reward_per_token;
  accounts.modify(acnt, 0,
                  [&](auto& account) { account.balance += asset(reward); });
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