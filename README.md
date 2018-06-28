# Resources exchange for EOS blockchain

### The aim of this contract is to create a self regulated market for CPU and Bandwidth on the eos blockchain

> This contract is under development

##### This contract allows:
- Leasing of EOS
- Renting of EOS
- Auto-renewals of the rent agreement
- Automatic repricing based on market state
- Timely withdraw of funds
- Generation of income from leasing


## Actions: 
 - deposit *(automatic when doing a transfer to contract)
 - withdraw: get fund out from exchange
 - buystake: stake net and cpu to your account
 - sellstake: cancel or reduce stake consumption
 
## How the exchange works

Users of this exchange shall deposit EOS in it, they will get an account inside the exchange which they can use.
This account holds the owner's name, balance and resources consuming.

Each N days the contract will execute the function cycle(). This will bill the users consuming resources or the new users that want to get resources the price for the next N days. During this period the user can use the resources without any additional charge. After the N days the user will be billed again.

In between this cycles the user may wish to cancel its resource plan (or change the amount of resources they want to rent), this action will be stored and queued to be performed on the next cycle. 

Actions to withdraw* and deposit will be executed immediately.

The pricing of the resources is done dynamically based on the exchange capacity and usage and will increase exponentially as the liquid funds of the exchange run out

Users who want to profit from renting EOS may do so by depositing in the exchange. After each cycle the profits from the fees will be awarded accordingly to the users balance, this also affects users renting resources from the network. Effectively incentivising renters to store resources on the exchange instead of staking them.

> For any question ask: @alepacheco on telegram