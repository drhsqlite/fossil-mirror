# Fossil As Blockchain

Fossil is a version control system built around blockchain.

Wikipedia defines "blockchain" as

>
  "a growing list of records, called blocks, which are linked using
   cryptography. Each block contains a cryptographic hash of the previous
   block, a timestamp, and transaction data..." [(1)][]


By that definition, Fossil is clearly an implementation of blockchain.
The blocks are ["manifests" artifacts](./fileformat.wiki#manifest).
Each manifest has a SHA1 or SHA3 hash of its parent or parents,
a timestamp, and other transactional data.  The repository grows by
add new manifests onto the list.

Some people have come to associate blockchain with cryptocurrency, however,
and since Fossil has nothing to do with cryptocurrency, the claim that
Fossil is build around blockchain is met with skepticism.  The key thing
to note here is that cryptocurrency implementations like BitCoin are
built around blockchain, but they are not synonymous with blockchain.
Blockchain is a much broader concept.  Blockchain is a mechanism for
constructed a distributed ledger of transactions.
Yes, you can use a distributed
ledger to implement a cryptocurrency, but you can also use a distributed
ledger to implement a version control system, and probably many other kinds
of applications as well.  Blockchain is a much broader idea than
cryptocurrency.

[(1)]: https://en.wikipedia.org/wiki/Blockchain
