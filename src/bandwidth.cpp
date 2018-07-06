#pragma once
#include "resource_exchange.hpp"

namespace eosio {

/**
 * Delegatebw is a shortcut for the delegatebw action
 **/
void resource_exchange::delegatebw(account_name receiver,
                                   asset stake_net_quantity,
                                   asset stake_cpu_quantity) {
  action(permission_level(_contract, N(active)), N(eosio), N(delegatebw),
         std::make_tuple(_contract, receiver, stake_net_quantity,
                         stake_cpu_quantity, false))
      .send();
}

/**
 * Unelegatebw is a shortcut for the unelegatebw action
 **/
void resource_exchange::undelegatebw(account_name receiver,
                                     asset stake_net_quantity,
                                     asset stake_cpu_quantity) {
  action(permission_level(_contract, N(active)), N(eosio), N(undelegatebw),
         std::make_tuple(_contract, receiver, stake_net_quantity,
                         stake_cpu_quantity))
      .send();
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
      asset undelegating = delegated->net_weight + delegated->cpu_weight;
      state_on_undelegate_unknown(undelegating);
    }
  }
}

}  // namespace eosio