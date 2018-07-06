#pragma once
#include "resource_exchange.hpp"
#include "state_manager.cpp"

namespace eosio {
/**
 * Buystake will schedule a purchase of stake for the next cycle,
 * it will update an existing scheduled purchase or create a new one
 * and will set the funds as staked
 **/
void resource_exchange::buystake(account_name from, asset net, asset cpu) {
  eosio_assert(net.is_valid() && cpu.is_valid(), "invalid quantity");
  eosio_assert(net.symbol == asset().symbol && cpu.symbol == asset().symbol,
               "asset must be system token");
  eosio_assert(net >= asset(0) && cpu >= asset(0) && (net + cpu) > asset(0),
               "must buy positive stake");

  auto itr = accounts.find(from);
  eosio_assert(itr != accounts.end(), "account not found");

  auto pending_itr = pendingtxs.find(from);
  if (pending_itr == pendingtxs.end()) {
    pending_itr = pendingtxs.emplace(from, [&](auto& tx) {
      tx.user = from;
      tx.net = asset(0);
      tx.cpu = asset(0);
    });
  }

  asset adj_net = net + pending_itr->net;
  asset adj_cpu = cpu + pending_itr->cpu;

  auto state = contract_state.get();
  eosio_assert(state.liquid_funds >= (adj_net + adj_cpu),
               "not enough resources in exchange");

  asset cost = calcost(adj_net + adj_cpu);
  eosio_assert(itr->balance >= cost, "not enough funds on account");

  print("Queing purchase of: ", net, " and ", cpu, " in stake for*: ", cost,
        "\n");
  pendingtxs.modify(pending_itr, 0, [&](auto& tx) {
    tx.net = adj_net;
    tx.cpu = adj_cpu;
  });

  state_on_buystake(net + cpu);
}

/**
 * Sellstake will remove resources used from a delayed tx if any
 * or sell the remove resources used from the account
 **/
void resource_exchange::sellstake(account_name user, asset net, asset cpu) {
  // to sell reduce account resources, in next cycle he will pay the new usage
  eosio_assert(net.is_valid() && cpu.is_valid(), "invalid quantity");
  eosio_assert(net >= asset(0) && cpu >= asset(0) && (net + cpu) > asset(0),
               "must sell positive stake amount");
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
               "not enough to sell");

  // reduce first in tx then in account
  if (pending_itr == pendingtxs.end()) {
    accounts.modify(itr, 0, [&](auto& acnt) {
      acnt.resource_net -= usr_net;
      acnt.resource_cpu -= usr_cpu;
    });
  } else {
    auto amount_tx_net = pending_itr->net - net;
    auto amount_tx_cpu = pending_itr->cpu - cpu;

    if (amount_tx_cpu >= asset(0)) {  // if enough to cover with tx
      pendingtxs.modify(pending_itr, 0,
                        [&](auto& tx) { tx.cpu = amount_tx_cpu; });
    } else {  // split with tx and account
      pendingtxs.modify(pending_itr, 0, [&](auto& tx) { tx.cpu = asset(0); });
      accounts.modify(itr, 0,
                      [&](auto& acnt) { acnt.resource_cpu += amount_tx_cpu; });
    }

    if (amount_tx_net >= asset(0)) {
      pendingtxs.modify(pending_itr, 0,
                        [&](auto& tx) { tx.net = amount_tx_net; });
    } else {
      pendingtxs.modify(pending_itr, 0, [&](auto& tx) { tx.net = asset(0); });
      accounts.modify(itr, 0,
                      [&](auto& acnt) { acnt.resource_net += amount_tx_net; });
    }

    if (pending_itr->is_empty()) {
      pendingtxs.erase(pending_itr);
    }
  }

  // TODO mark as liquid on the next cycle when refunded (not refunding here)
  state_on_sellstake(net + cpu);
}

}  // namespace eosio