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

  if (cpu_account > cpu_delegated) {
    cpu_to_delegate += (cpu_account - cpu_delegated);
  } else if (cpu_account < cpu_delegated) {
    cpu_to_undelegate += (cpu_delegated - cpu_account);
  }
  if ((net_to_delegate + cpu_to_delegate) > asset(0)) {
    delegatebw(user->owner, net_to_delegate, cpu_to_delegate);
  }
  if ((net_to_undelegate + cpu_to_undelegate) > asset(0)) {
    undelegatebw(user->owner, net_to_undelegate, cpu_to_undelegate);
  }
}

}  // namespace eosio