#include <boost/container/flat_map.hpp>
#include <cmath>
#include <eosiolib/currency.hpp>
#include <eosiolib/eosio.hpp>
#include <eosiolib/print.hpp>
#include <eosiolib/types.hpp>
#include "resource_exchange.hpp"

namespace eosio {
  void resource_exchange::apply(account_name contract, account_name act) {
    //print("Running: ", name{contract}, " - ", name{act}, "\n"); 
    switch (act) {
    case N(transfer): {
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
    }
  }

  void resource_exchange::deposit(currency::transfer tx) {
    require_auth(tx.from);
    eosio_assert(tx.quantity.is_valid(), "invalid quantity");
    eosio_assert(tx.quantity.symbol == asset().symbol, "asset must be system token");

    auto itr = accounts.find(tx.from);
    if (itr == accounts.end()) {
      itr = accounts.emplace(_self, [&](auto &acnt) {
        acnt.owner = tx.from;
        acnt.balance = asset(0);
        acnt.resource_cpu = 0;
        acnt.resource_net = 0;
      });
    }

    accounts.modify(itr, 0, [&](auto &acnt) { 
      acnt.balance += tx.quantity; 
    });
  }
  void resource_exchange::withdraw(account_name to, asset quantity) {
    // cleos set account permission {contract_name} active '{"threshold": 1,"keys": [{"key": "{key_for_permision}","weight": 1}],"accounts": [{"permission":{"actor":"{contract_name}","permission":"eosio.code"},"weight":1}]}' owner
    // print("Widrawing: ", quantity, " to: ", name{to}, "\n");
    require_auth(to);
    eosio_assert(quantity.is_valid(), "invalid quantity");
    eosio_assert(quantity.amount > 0, "must withdraw positive quantity");
    
    auto itr = accounts.find(to);
    eosio_assert(itr != accounts.end(), "unknown account");

    accounts.modify(itr, 0, [&](auto &acnt) {
      eosio_assert(acnt.balance >= quantity, "insufficient balance");
      acnt.balance -= quantity;
    });
    
    action(permission_level( _contract, N(active) ), 
          N(eosio.token), N(transfer), 
          std::make_tuple(_contract,to,quantity,std::string("")))
        .send();

    if(itr->is_empty()) {
      accounts.erase(itr);
    }
  }
}


extern "C" {
  [[noreturn]] void apply(uint64_t receiver, uint64_t code, uint64_t action) {
    eosio::resource_exchange ex(receiver);
    ex.apply(code, action);
    eosio_exit(0);
  }
}