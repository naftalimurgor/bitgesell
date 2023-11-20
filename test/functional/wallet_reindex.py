#!/usr/bin/env python3
# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.

"""Test wallet-reindex interaction"""

import time

from test_framework.blocktools import COINBASE_MATURITY
from test_framework.test_framework import BGLTestFramework
from test_framework.util import (
    assert_equal,
)
BLOCK_TIME = 60 * 10

class WalletReindexTest(BGLTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def advance_time(self, node, secs):
        self.node_time += secs
        node.setmocktime(self.node_time)

    # Verify the wallet updates the birth time accordingly when it detects a transaction
    # with a time older than the oldest descriptor timestamp.
    # This could happen when the user blindly imports a descriptor with 'timestamp=now'.
    def birthtime_test(self, node, miner_wallet):
        self.log.info("Test birth time update during tx scanning")
        # Fund address to test
        wallet_addr = miner_wallet.getnewaddress()
        tx_id = miner_wallet.sendtoaddress(wallet_addr, 2)

        # Generate 50 blocks, one every 10 min to surpass the 2 hours rescan window the wallet has
        for _ in range(50):
            self.generate(node, 1)
            self.advance_time(node, BLOCK_TIME)

        # Now create a new wallet, and import the descriptor
        node.createwallet(wallet_name='watch_only', disable_private_keys=True, blank=True, load_on_startup=True)
        wallet_watch_only = node.get_wallet_rpc('watch_only')

        # For a descriptors wallet: Import address with timestamp=now.
        # For legacy wallet: There is no way of importing a script/address with a custom time. The wallet always imports it with birthtime=1.
        # In both cases, disable rescan to not detect the transaction.
        wallet_watch_only.importaddress(wallet_addr, rescan=False)
        assert_equal(len(wallet_watch_only.listtransactions()), 0)

        # Rescan the wallet to detect the missing transaction
        wallet_watch_only.rescanblockchain()
        assert_equal(wallet_watch_only.gettransaction(tx_id)['confirmations'], 50)
        assert_equal(wallet_watch_only.getbalances()['mine' if self.options.descriptors else 'watchonly']['trusted'], 2)

        # Reindex and wait for it to finish
        with node.assert_debug_log(expected_msgs=["initload thread exit"]):
            self.restart_node(0, extra_args=['-reindex=1', f'-mocktime={self.node_time}'])
        node.syncwithvalidationinterfacequeue()

        # Verify the transaction is still 'confirmed' after reindex
        wallet_watch_only = node.get_wallet_rpc('watch_only')
        assert_equal(wallet_watch_only.gettransaction(tx_id)['confirmations'], 50)

        wallet_watch_only.unloadwallet()

    def run_test(self):
        node = self.nodes[0]
        self.node_time = int(time.time())
        node.setmocktime(self.node_time)

        # Fund miner
        node.createwallet(wallet_name='miner', load_on_startup=True)
        miner_wallet = node.get_wallet_rpc('miner')
        self.generatetoaddress(node, COINBASE_MATURITY + 10, miner_wallet.getnewaddress())

        # Tests
        self.birthtime_test(node, miner_wallet)


if __name__ == '__main__':
    WalletReindexTest().main()
