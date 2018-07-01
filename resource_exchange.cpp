#include "resource_exchange.hpp"
#include <cmath>
#include <eosiolib/currency.hpp>
#include <eosiolib/eosio.hpp>
#include <eosiolib/print.hpp>
#include <eosiolib/time.hpp>
#include <eosiolib/transaction.hpp>
#include <eosiolib/types.hpp>

namespace eosio {
void resource_exchange::apply(account_name contract, account_name act) {
  if (!contract_state.exists()) {
    contract_state.set(state_t{asset(0), asset(0), time_point_sec(0)},
                       _contract);
  }

  switch (act) {
    case N(transfer): {
      eosio_assert(contract == N(eosio.token),
                   "invalid contract, use eosio.token");
      auto tx = unpack_action_data<currency::transfer>();
      if (tx.from != _contract) {
        require_auth(tx.from);
        deposit(tx);
      }
      break;
    }
    case N(withdraw): {
      auto tx = unpack_action_data<withdraw_tx>();
      require_auth(tx.user);
      withdraw(tx.user, tx.quantity);
      break;
    }
    case N(buystake): {
      auto tx = unpack_action_data<stake_trade>();
      require_auth(tx.user);
      buystake(tx.user, tx.net, tx.cpu);
      break;
    }
    case N(sellstake): {
      auto tx = unpack_action_data<stake_trade>();
      require_auth(tx.user);
      sellstake(tx.user, tx.net, tx.cpu);
      break;
    }
    case N(cycle): {
      require_auth(_contract);
      cycle();
      break;
    }
  }
}

/**
 * When a tx is received, the exchange will create an account or find an
 *existing one and add the amount to the account and to the liquid state
 **/
void resource_exchange::deposit(currency::transfer tx) {
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
  contract_state.set(state_t{state.liquid_funds + tx.quantity,
                             state.total_stacked, state.timestamp},
                     _self);
}

/**
 * Withdraw deducts the amount from the user account and from the liquid state
 * this funds will not be considered as part of the exchange, on the next cycle
 *the price will increase, reducing the resource consuption and the tokens will
 *be unstaked and accesible on 3 days
 **/
void resource_exchange::withdraw(account_name to, asset quantity) {
  eosio_assert(quantity.is_valid(), "invalid quantity");
  eosio_assert(quantity.amount > 0, "must withdraw positive quantity");

  auto state = contract_state.get();
  auto itr = accounts.find(to);
  print(name{to});
  eosio_assert(itr != accounts.end(), "unknown account");

  accounts.modify(itr, 0, [&](auto& acnt) {
    eosio_assert(acnt.balance >= quantity, "insufficient balance");
    acnt.balance -= quantity;
  });

  // Update contract state
  contract_state.set(state_t{state.liquid_funds - quantity, state.total_stacked,
                             state.timestamp},
                     _self);

  // if liquid withdraw now, else wait tree days
  auto liquid_funds = contract_balance.find(asset().symbol.name())->balance;
  if (quantity <= liquid_funds) {
    action(permission_level(_contract, N(active)), N(eosio.token), N(transfer),
           std::make_tuple(_contract, to, quantity, std::string("")))
        .send();
  } else {
    eosio::transaction out;
    out.actions.emplace_back(
        permission_level(_contract, N(active)), N(eosio.token), N(transfer),
        std::make_tuple(_contract, to, quantity, std::string("")));

    out.delay_sec = 60 * 60 * 24 * 3 + 100;  // Three days plus safety seconds
    out.send(to, _contract);
  }

  if (itr->is_empty()) {
    accounts.erase(itr);
  }
}

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

  contract_state.set(
      state_t{state.liquid_funds - (net + cpu),
              state.total_stacked + (net + cpu), state.timestamp},
      _self);
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

  auto state = contract_state.get();
  contract_state.set(
      state_t{state.liquid_funds + (net + cpu),
              state.total_stacked - (net + cpu), state.timestamp},
      _self);
}

/**
 * Reset_delayed_tx will revert the state when a delayed tx cannot be completed
 **/
void resource_exchange::reset_delayed_tx(pendingtx tx) {
  auto state = contract_state.get();
  contract_state.set(
      state_t{state.liquid_funds + asset(tx.net.amount + tx.cpu.amount),
              state.total_stacked - asset(tx.net.amount + tx.cpu.amount),
              state.timestamp},
      _self);
}
void resource_exchange::cycle() {
  print("Run cycle\n");
  auto secs_to_next = time_point_sec(60); // Run every minute
                                          // * TODO tune 
  auto secs_flexibility = time_point_sec(5);
  auto state = contract_state.get();
  time_point_sec this_time = time_point_sec(now());
  eosio::transaction out;
  out.actions.emplace_back(permission_level(_contract, N(active)), _contract,
                           N(cycle), _contract);

  if ((state.timestamp.utc_seconds + secs_to_next.utc_seconds) >
      this_time.utc_seconds) {
    out.delay_sec = (state.timestamp.utc_seconds + secs_to_next.utc_seconds) -
                    (this_time.utc_seconds + secs_flexibility.utc_seconds);
    out.send(this_time.utc_seconds, _contract);
    return;
  }

  double cost_per_token = calcosttoken();
  unstakeunknown();
  for (auto acnt = accounts.begin(); acnt != accounts.end(); ++acnt) {
    billaccount(acnt->owner, cost_per_token);
    matchbandwidth(acnt->owner);
    payreward(acnt->owner, cost_per_token);
  }

  out.delay_sec = secs_to_next.utc_seconds;
  out.send(this_time.utc_seconds, _contract);

  // set timestamp
  contract_state.set(
      state_t{state.liquid_funds, state.total_stacked, this_time}, _self);
}

void resource_exchange::billaccount(account_name owner, double cost_per_token) {
  auto acnt = accounts.find(owner);
  auto pending_itr = pendingtxs.find(acnt->owner);

  auto cost_all = asset(cost_per_token * acnt->get_all().amount);
  asset extra_net = asset(0);
  asset extra_cpu = asset(0);

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
  } else {
    if (pending_itr != pendingtxs.end()) {
      reset_delayed_tx(*pending_itr);
    }
    asset cost_account = asset(cost_per_token * acnt->get_all().amount);
    if (acnt->balance >= cost_account) {
      accounts.modify(acnt, 0,
                      [&](auto& account) { account.balance -= cost_account; });
    } else {
      // no balance: reset account
      auto state = contract_state.get();
      contract_state.set(
          state_t{
              state.liquid_funds + (acnt->resource_net + acnt->resource_cpu),
              state.total_stacked - (acnt->resource_net + acnt->resource_cpu),
              state.timestamp},
          _self);
      accounts.modify(acnt, 0, [&](auto& account) {
        account.resource_net = asset(0);
        account.resource_cpu = asset(0);
      });
    }
  }
  if (pending_itr != pendingtxs.end()) {
    pendingtxs.erase(pending_itr);
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

  if (cpu_account > net_delegated) {
    cpu_to_delegate += (cpu_account - cpu_delegated);
  } else if (net_account < net_delegated) {
    cpu_to_undelegate += (cpu_delegated - cpu_account);
  }
  if ((net_to_delegate + cpu_to_delegate) > asset(0)) {
    delegatebw(user->owner, net_to_delegate, cpu_to_delegate);
  }
  if ((net_to_undelegate + cpu_to_undelegate) > asset(0)) {
    undelegatebw(user->owner, net_to_undelegate, cpu_to_undelegate);
  }
}

void resource_exchange::payreward(account_name user, double cost_per_token) {
  auto state = contract_state.get();
  const double multiplier = cost_per_token * 0.9;
  auto total_reward = state.total_stacked.amount * multiplier;
  auto reward_per_token = total_reward / state.get_total().amount;
  auto acnt = accounts.find(user);
  auto reward = acnt->balance.amount * reward_per_token;

  accounts.modify(acnt, 0,
                  [&](auto& account) { account.balance += asset(reward); });
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
      auto state = contract_state.get();
      contract_state.set(state_t{state.liquid_funds + (delegated->net_weight +
                                                       delegated->cpu_weight),
                                 state.total_stacked - (delegated->net_weight +
                                                        delegated->cpu_weight),
                                 state.timestamp},
                         _self);
    }
  }
}
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

double resource_exchange::calcosttoken() {
  eosio_assert(contract_state.exists(), "No contract state available");
  int PRICE_TUNE = 10000;
  auto state = contract_state.get();
  double liquid = state.liquid_funds.amount;
  double total = state.get_total().amount;
  double cost_per_token = ((total * total / liquid) /
                           (total * PRICE_TUNE));  // TODO find optimal function

  print("price token: ", asset(cost_per_token * 10000), "\n");
  return cost_per_token;
}
}  // namespace eosio

extern "C" {
[[noreturn]] void apply(uint64_t receiver, uint64_t code, uint64_t action) {
  eosio::resource_exchange ex(receiver);
  ex.apply(code, action);
  eosio_exit(0);
}
}