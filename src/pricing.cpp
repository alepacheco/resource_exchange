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

  print("price token: ", asset(cost_per_token * 10000), "::", cost_per_token,
        "\n");
  return cost_per_token;
}

void resource_exchange::payreward(account_name user, asset fee_collected) {
  auto state = contract_state.get();
  double fees = fee_collected.amount * 1;
  double reward_per_token = fees / state.get_total().amount;
  auto acnt = accounts.find(user);
  double reward = acnt->balance.amount * reward_per_token;
  accounts.modify(acnt, 0,
                  [&](auto& account) { account.balance += asset(reward); });
}

}  // namespace eosio