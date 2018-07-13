#include "resource_exchange.hpp"
#include "accounts.cpp"
#include "bandwidth.cpp"
#include "pricing.cpp"
#include "stake.cpp"
#include "state_manager.cpp"

namespace eosio {
void resource_exchange::apply(account_name contract, account_name act) {
  state_init();

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
    case N(calcosttoken): {
      calcosttoken();
      break;
    }
  }
}

void resource_exchange::cycle() {
  print("Run cycle\n");
  auto secs_to_next = time_point_sec(CYCLE_TIME);
  auto secs_flexibility = time_point_sec(5);
  auto state = contract_state.get();
  time_point_sec this_time = time_point_sec(now());
  eosio::transaction out;
  out.actions.emplace_back(permission_level(_contract, N(active)), _contract,
                           N(cycle), _contract);

  docycle();

  out.delay_sec = secs_to_next.utc_seconds;
  out.send(this_time.utc_seconds, _contract);

  state_set_timestamp(this_time);
}

void resource_exchange::docycle() {
  double cost_per_token = calcosttoken();
  asset fees_collected = asset(0);
  for (auto acnt = accounts.begin(); acnt != accounts.end(); ++acnt) {
    fees_collected += billaccount(acnt->owner, cost_per_token);
    matchbandwidth(acnt->owner);
  }
  asset fees_devs = asset(fees_collected.amount * 0.0);
  for (auto acnt = accounts.begin(); acnt != accounts.end(); ++acnt) {
    payreward(acnt->owner, fees_collected - fees_devs);
  }
  unstakeunknown();

  // TODO paydevs
  state_cycle();

  print("Total fees: ", fees_collected, " ");
}

}  // namespace eosio

extern "C" {
[[noreturn]] void apply(uint64_t receiver, uint64_t code, uint64_t action) {
  eosio::resource_exchange ex(receiver);
  ex.apply(code, action);
  eosio_exit(0);
}
}