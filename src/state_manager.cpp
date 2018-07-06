#pragma once
#include "resource_exchange.hpp"

namespace eosio {
void resource_exchange::state_init() {
  if (!contract_state.exists()) {
    contract_state.set(
        state_t{asset(0), asset(0), time_point_sec(0), asset(0), asset(0)},
        _contract);
  }
}

void resource_exchange::state_change(asset liquid, asset staked) {
  auto state = contract_state.get();
  contract_state.set(
      state_t{state.liquid_funds + liquid, state.total_stacked + staked,
              state.timestamp, state.to_be_refunding, state.refunding},
      _self);
}

void resource_exchange::state_unstake_delayed(asset amount) {
  eosio_assert(amount >= asset(0), "must use positive amount");
  auto state = contract_state.get();
  contract_state.set(
      state_t{state.liquid_funds, state.total_stacked - amount, state.timestamp,
              state.to_be_refunding + amount, state.refunding},
      _self);
}

void resource_exchange::state_cycle() {
  auto state = contract_state.get();
  contract_state.set(
      state_t{state.liquid_funds + state.refunding, state.total_stacked,
              state.timestamp, asset(0), state.to_be_refunding},
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
  contract_state.set(state_t{state.liquid_funds, state.total_stacked, this_time,
                             state.to_be_refunding, state.refunding},
                     _self);
}

void resource_exchange::state_on_undelegate_unknown(asset delegated) {
  state_unstake_delayed(delegated);
}

void resource_exchange::state_on_reset_account(asset account_res) {
  state_unstake_delayed(account_res);
}

void resource_exchange::state_on_buystake(asset stake) {
  state_change(-stake, stake);
}

void resource_exchange::state_on_sellstake(asset stake_from_account,
                                           asset stake_from_tx) {
  state_change(stake_from_tx, -stake_from_tx);
  state_unstake_delayed(stake_from_account);
}

}  // namespace eosio