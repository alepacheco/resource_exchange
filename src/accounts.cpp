#pragma once
#include "resource_exchange.hpp"
#include "state_manager.cpp"

namespace eosio {
/**
 * When a tx is received, the exchange will create an account or find an
 *existing one and add the amount to the account and to the liquid state
 **/
void resource_exchange::deposit(currency::transfer tx) {
  eosio_assert(tx.quantity.is_valid(), "invalid quantity");
  eosio_assert(tx.quantity.symbol == asset().symbol,
               "asset must be system token");

  auto itr = accounts.find(tx.from);

  if (itr == accounts.end()) {
    itr = accounts.emplace(tx.from, [&](auto& acnt) { acnt.owner = tx.from; });
  }

  accounts.modify(itr, 0, [&](auto& acnt) { acnt.balance += tx.quantity; });

  state_on_deposit(tx.quantity);
}

/**
 * Withdraw deducts the amount from the user account and from the liquid state
 * this funds will not be considered as part of the exchange, on the next cycle
 * the price will increase, reducing the resource consuption and the tokens will
 * be unstaked and accesible on 3 days
 **/
void resource_exchange::withdraw(account_name to, asset quantity) {
  // TODO cancel buy tx if cant pay for it
  eosio_assert(quantity.is_valid(), "invalid quantity");
  eosio_assert(quantity.amount > 0, "must withdraw positive quantity");
  auto state = contract_state.get();

  if (quantity > state.liquid_funds) {
    if (quantity > state.get_unstaked()) {
      // force overdraft
      eosio_assert(false, "cannot withdraw");
    } else {
      // retry next cycle
      eosio::transaction out;
      out.actions.emplace_back(permission_level(to, N(active)), _self,
                               N(withdraw), std::make_tuple(to, quantity));

      out.delay_sec = CYCLE_TIME + 100;
      out.send(to, _contract);
      return;
    }
  }

  auto itr = accounts.find(to);
  eosio_assert(itr != accounts.end(), "unknown account");

  accounts.modify(itr, 0, [&](auto& acnt) {
    eosio_assert(acnt.balance >= quantity, "insufficient balance");
    acnt.balance -= quantity;
  });

  state_on_withdraw(quantity);

  action(permission_level(_contract, N(active)), N(eosio.token), N(transfer),
         std::make_tuple(_contract, to, quantity, std::string("")))
      .send();

  if (itr->is_empty()) {
    accounts.erase(itr);
  }
}

}  // namespace eosio