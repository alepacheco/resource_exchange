#include <eosiolib/types.hpp>
#include <eosiolib/currency.hpp>
#include <eosiolib/eosio.hpp>

namespace eosio {
class resource_exchange : public eosio::contract {
private:
  account_name _contract;

public:
  resource_exchange(account_name self) : _contract(self), eosio::contract(self), accounts(_self, _self) {}

  struct withdraw_tx {
    account_name to;
    asset quantity;
  };

  //@abi table account i64
  struct account {
    account(account_name o = account_name()) : owner(o) {}
    account_name owner;
    asset balance;
    uint32_t resource_net = 0;
    uint32_t resource_cpu = 0;

    bool is_empty() const {
      return !(balance.amount | resource_net | resource_cpu);
    }

    uint64_t primary_key() const { return owner; }
    EOSLIB_SERIALIZE(account, (owner)(balance)(resource_net)(resource_cpu))
  };

  typedef eosio::multi_index<N(account), account> account_index;
  account_index accounts;

  void apply(account_name contract, account_name act);
  void deposit(currency::transfer tx);

  /// @abi action
  void withdraw(account_name to, asset quantity);
};
} // namespace eosio
