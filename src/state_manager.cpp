#pragma once
#include "resource_exchange.hpp"

namespace eosio {
void resource_exchange::state_init() {
  if (!contract_state.exists()) {
    contract_state.set(state_t{asset(0), asset(0), time_point_sec(0)},
                       _contract);
  }
}

void resource_exchange::state_change(asset liquid, asset staked) {
  auto state = contract_state.get();
  contract_state.set(state_t{state.liquid_funds + liquid,
                             state.total_stacked + staked, state.timestamp},
                     _self);
}

void resource_exchange::state_on_deposit(asset quantity) {
  state_change(quantity, asset(0));
}

void resource_exchange::state_on_withdraw(asset quantity) {
  state_change(-quantity, asset(0));
}

void resource_exchange::reset_delayed_tx(pendingtx tx) {
  asset tx_amount = asset(tx.net.amount + tx.cpu.amount);
  state_change(tx_amount, -tx_amount);
}

void resource_exchange::state_set_timestamp(time_point_sec this_time) {
  auto state = contract_state.get();
  contract_state.set(
      state_t{state.liquid_funds, state.total_stacked, this_time}, _self);
}


// TODO 3 days delay
void resource_exchange::state_on_undelegate_unknown(asset delegated) {
  state_change(delegated, -delegated);
}

// TODO 3 days delay
void resource_exchange::state_on_reset_account(asset account_res) {
  state_change(account_res, -account_res);
}

void resource_exchange::state_on_buystake(asset stake) {
  // Mark as staked, but it can later fail if not enought funds
  state_change(-stake, stake);
}


void resource_exchange::state_on_sellstake(asset stake_from_account, asset stake_from_tx) {
  state_change(stake_from_tx, -stake_from_tx);
  
  // TODO 3 days delay (to count on next cycle)
  state_change(stake_from_account, -stake_from_account);
}

}  // namespace eosio