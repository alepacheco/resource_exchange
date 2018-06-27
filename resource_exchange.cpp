#include "resource_exchange.hpp"
#include <cmath>
#include <eosiolib/currency.hpp>
#include <eosiolib/eosio.hpp>
#include <eosiolib/print.hpp>
#include <eosiolib/types.hpp>

namespace eosio {
void resource_exchange::apply(account_name contract, account_name act) {
  if (!contract_state.exists()) {
    contract_state.set(state_t{asset(0), asset(0)}, _self);
  }

  switch (act) {
    case N(transfer): {
      eosio_assert(contract == N(eosio.token),
                   "invalid contract, use eosio.token");
      auto tx = unpack_action_data<currency::transfer>();
      if (tx.from != _contract) {
        deposit(tx);
      }
      break;
    }
    case N(withdraw): {
      auto tx = unpack_action_data<withdraw_tx>();
      withdraw(tx.to, tx.quantity);
      break;
    }
    case N(buystake): {
      auto tx = unpack_action_data<stake_trade>();
      buystake(tx.from, tx.net, tx.cpu);
      break;
    }
    case N(calcost): {
      auto tx = unpack_action_data<asset>();
      calcost(tx);
      break;
    }
  }
}

void resource_exchange::deposit(currency::transfer tx) {
  require_auth(tx.from);
  eosio_assert(tx.quantity.is_valid(), "invalid quantity");
  eosio_assert(tx.quantity.symbol == asset().symbol,
               "asset must be system token");

  auto itr = accounts.find(tx.from);
  // Create new account if it doesn't exist
  if (itr == accounts.end()) {
    itr = accounts.emplace(tx.from, [&](auto& acnt) { acnt.owner = tx.from; });
  }

  // Update account
  accounts.modify(itr, 0, [&](auto& acnt) { acnt.balance += tx.quantity; });

  // Update contract state
  auto state = contract_state.get();
  contract_state.set(
      state_t{state.liquid_funds + tx.quantity, state.total_stacked}, _self);
}

void resource_exchange::withdraw(account_name to, asset quantity) {
  require_auth(to);
  eosio_assert(quantity.is_valid(), "invalid quantity");
  eosio_assert(quantity.amount > 0, "must withdraw positive quantity");

  auto itr = accounts.find(to);
  eosio_assert(itr != accounts.end(), "unknown account");

  accounts.modify(itr, 0, [&](auto& acnt) {
    eosio_assert(acnt.balance >= quantity, "insufficient balance");
    acnt.balance -= quantity;
  });

  // Update contract state
  auto state = contract_state.get();
  contract_state.set(
      state_t{state.liquid_funds - quantity, state.total_stacked}, _self);

  // TODO first unstake some tokens, 3days later send
  action(permission_level(_contract, N(active)), N(eosio.token), N(transfer),
         std::make_tuple(_contract, to, quantity, std::string("")))
      .send();

  if (itr->is_empty()) {
    accounts.erase(itr);
  }
}

void resource_exchange::delegatebw(account_name receiver,
                                   asset stake_net_quantity,
                                   asset stake_cpu_quantity) {
  action(permission_level(_contract, N(active)), N(eosio), N(delegatebw),
         std::make_tuple(_contract, receiver, stake_net_quantity,
                         stake_cpu_quantity, false))
      .send();
}
void resource_exchange::undelegatebw(account_name receiver,
                                     asset stake_net_quantity,
                                     asset stake_cpu_quantity) {
  action(permission_level(_contract, N(active)), N(eosio), N(undelegatebw),
         std::make_tuple(_contract, receiver, stake_net_quantity,
                         stake_cpu_quantity))
      .send();
}

void resource_exchange::buystake(account_name from, asset net, asset cpu) {
  require_auth(from);
  eosio_assert(net.is_valid() && cpu.is_valid(), "invalid quantity");
  eosio_assert(net.symbol == asset().symbol && cpu.symbol == asset().symbol,
               "asset must be system token");

  auto itr = accounts.find(from);
  eosio_assert(itr != accounts.end(), "account not found");

  auto pending_itr = pendingtxs.find(from);
  if (pending_itr == pendingtxs.end()) {
    pending_itr = pendingtxs.emplace(from, [&](auto& tx) {
      tx.user = from;
      tx.net = asset(0);
      tx.cpu = asset(0);
      tx.withdrawing = asset(0);
    });
  }

  asset adj_net = net + pending_itr->net;
  asset adj_cpu = cpu + pending_itr->cpu;

  auto state = contract_state.get();
  eosio_assert(state.liquid_funds >= (adj_net + adj_cpu),
               "not enough resources in exchange");

  asset cost = calcost(adj_net + adj_cpu);
  eosio_assert((itr->balance - pending_itr->withdrawing) >= cost,
               "not enough funds on account");

  print("Queing purchase of: ", net + cpu, " in stake for*: ", cost, "\n");
  pendingtxs.modify(pending_itr, 0, [&](auto& tx) {
    tx.net = adj_net;
    tx.cpu = adj_cpu;
  });

  // TODO (reset if then fails) #dobuystake
  contract_state.set(state_t{state.liquid_funds - (net + cpu),
                             state.total_stacked + (net + cpu)},
                     _self);
}

void resource_exchange::dobuystake(account_name user, asset net, asset cpu) {
  // auto itr = accounts.find(from);
  // eosio_assert(itr != accounts.end(), "account not found");

  // asset cost = calcost(net + cpu);
  // if (itr->balance >= cost) {
  //   print("Buying: ", net + cpu, " in stake for: ", cost, "\n");
  //   // deduce account
  //   accounts.modify(itr, 0, [&](auto& acnt) {
  //     acnt.balance -= cost;
  //     acnt.resource_net += net;
  //     acnt.resource_cpu += cpu;
  //   });
  //   // delegate resources
  //   delegatebw(to, net, cpu);
  // } else {
  // }

  // if no cash, remove state staked
}

void resource_exchange::sellstake(account_name user, asset net, asset cpu) {
  // to sell reduce account resources, in next cycle he will pay the new usage
  require_auth(user);
  eosio_assert(net.is_valid() && cpu.is_valid(), "invalid quantity");
  eosio_assert(net.symbol == asset().symbol && cpu.symbol == asset().symbol,
               "asset must be system token");

  auto itr = accounts.find(user);
  auto pending_itr = pendingtxs.find(user);
  eosio_assert(itr != accounts.end(), "unknown account");
  asset usr_cpu = itr->resource_cpu;
  asset usr_net = itr->resource_net;

  if (pending_itr != pendingtxs.end()) {
    usr_cpu += pending_itr->cpu;
    usr_net += pending_itr->net;
  }

  eosio_assert(usr_cpu >= cpu && usr_net >= net && (net + cpu) > asset(0),
               "not enought to sell");

  // reduce first in tx then in account
  if (pending_itr == pendingtxs.end()) {
    accounts.modify(itr, 0, [&](auto& acnt) {
      acnt.resource_net -= usr_net;
      acnt.resource_cpu -= usr_cpu;
    });
  } else {
    auto amount_tx_net = pending_itr->net - net;
    auto amount_tx_cpu = pending_itr->cpu - cpu;

    if (amount_tx_cpu >= asset(0)) {  // if enought to cover with tx
      pendingtxs.modify(pending_itr, 0,
                        [&](auto& tx) { tx.cpu -= amount_tx_cpu; });
    } else {  // split with tx and account
      pendingtxs.modify(pending_itr, 0, [&](auto& tx) { tx.cpu = asset(0); });
      accounts.modify(itr, 0,
                      [&](auto& acnt) { acnt.resource_cpu += amount_tx_cpu; });
    }

    if (amount_tx_net >= asset(0)) {
      pendingtxs.modify(pending_itr, 0,
                        [&](auto& tx) { tx.net -= amount_tx_net; });
    } else {
      pendingtxs.modify(pending_itr, 0, [&](auto& tx) { tx.net = asset(0); });
      accounts.modify(itr, 0,
                      [&](auto& acnt) { acnt.resource_net += amount_tx_net; });
    }
  }

  auto state = contract_state.get();
  contract_state.set(state_t{state.liquid_funds + (net + cpu),
                             state.total_stacked - (net + cpu)},
                     _self);
}

void resource_exchange::cycle() {
  // TODO
  // remove sold stake from users (deduce, undelegate)
  // finish stake purchase orders (deduce, delegate)
  // bill accounts with stake
  // pay hodlers
}

// TODO find optimal function
asset resource_exchange::calcost(asset resources) {
  eosio_assert(contract_state.exists(), "No contract state available");
  int PURCHASE_STEP = 10000;  // the lower the more persice but more cpu
  int PRICE_TUNE = 100;
  auto state = contract_state.get();
  uint64_t purchase = resources.amount;
  double_t liquid = state.liquid_funds.amount;
  double_t total = state.get_total().amount;

  // purchases of 1 EOS at a time, to get fair price as the resources get
  // scarcer
  uint32_t steps = purchase / PURCHASE_STEP;
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
}  // namespace eosio

extern "C" {
[[noreturn]] void apply(uint64_t receiver, uint64_t code, uint64_t action) {
  eosio::resource_exchange ex(receiver);
  ex.apply(code, action);
  eosio_exit(0);
}
}