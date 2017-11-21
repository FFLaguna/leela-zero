#!/usr/bin/env python3
#
#    This file is part of Leela Zero.
#    Copyright (C) 2017 Gian-Carlo Pascutto
#
#    Leela Zero is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    Leela Zero is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with Leela Zero.  If not, see <http://www.gnu.org/licenses/>.

import sys
import glob
import gzip
import random
import math
import multiprocessing as mp
import tensorflow as tf
from tfprocess import TFProcess

# 16 planes, 1 stm, 1 x BOARD_ACTION_N probs, 1 winner = BOARD_SIZE lines
DATA_ITEM_LINES = 16 + 1 + 1 + 1

BATCH_SIZE = 256
BOARD_SIZE = 9
BOARD_SQUARE_SIZE = BOARD_SIZE * BOARD_SIZE
BOARD_ACTION_N = BOARD_SQUARE_SIZE + 1  # 1 for "PASS"

def remap_vertex(vertex, symmetry):
    """
        Remap a go board coordinate according to a symmetry.
    """
    assert vertex >= 0 and vertex < BOARD_SQUARE_SIZE
    x = vertex % BOARD_SIZE
    y = vertex // BOARD_SIZE
    if symmetry >= 4:
        x, y = y, x
        symmetry -= 4
    if symmetry == 1 or symmetry == 3:
        x = BOARD_SIZE - x - 1
    if symmetry == 2 or symmetry == 3:
        y = BOARD_SIZE - y - 1
    return y * BOARD_SIZE + x

def apply_symmetry(plane, symmetry):
    """
        Applies one of 8 symmetries to the go board.

        The supplied go board can have BOARD_SQUARE_SIZE or BOARD_ACTION_N elements. The BOARD_ACTION_Nth
        element is pass will which get the identity mapping.
    """
    assert symmetry >= 0 and symmetry < 8
    work_plane = [0.0] * BOARD_SQUARE_SIZE
    for vertex in range(0, BOARD_SQUARE_SIZE):
        work_plane[vertex] = plane[remap_vertex(vertex, symmetry)]
    # Map back "pass"
    if len(plane) == BOARD_ACTION_N:
        work_plane.append(plane[BOARD_SQUARE_SIZE])
    return work_plane

def convert_train_data(text_item):
    """"
        Convert textual training data to python lists.

        Converts a set of BOARD_SIZE lines of text into a pythonic dataformat.
        [[plane_1],[plane_2],...],...
        [probabilities],...
        winner,...
    """
    planes = []
    for plane in range(0, 16):
        hex_len = BOARD_SQUARE_SIZE // 4
        # convert leading $hex_len hex chars to 4*$hen_len bin chars
        hex_string = text_item[plane][0:hex_len]
        integer = int(hex_string, 16)
        as_str = format(integer, f'0>{hex_len*4}b')
        # remaining bit that didn't fit.
        if BOARD_SIZE % 2 == 1:
            last_digit = text_item[plane][hex_len]
            assert last_digit == "0" or last_digit == "1"
            as_str += last_digit
        assert len(as_str) == BOARD_SQUARE_SIZE
        plane = [0.0 if digit == "0" else 1.0 for digit in as_str]
        planes.append(plane)
    stm = text_item[16][0]
    assert stm == "0" or stm == "1"
    if stm == "0":
        planes.append([1.0] * BOARD_SQUARE_SIZE)
        planes.append([0.0] * BOARD_SQUARE_SIZE)
    else:
        planes.append([0.0] * BOARD_SQUARE_SIZE)
        planes.append([1.0] * BOARD_SQUARE_SIZE)
    assert len(planes) == 18
    probabilities = []
    for val in text_item[17].split():
        float_val = float(val)
        # Work around a bug in leela-zero v0.3
        if math.isnan(float_val):
            return False, None
        probabilities.append(float_val)
    assert len(probabilities) == BOARD_ACTION_N
    winner = float(text_item[18])
    assert winner == 1.0 or winner == -1.0
    # Get one of 8 symmetries
    symmetry = random.randrange(8)
    sym_planes = [apply_symmetry(plane, symmetry) for plane in planes]
    sym_probabilities = apply_symmetry(probabilities, symmetry)
    return True, (sym_planes, sym_probabilities, [winner])

class ChunkParser:
    def __init__(self, chunks, max_workers=None):
        self.queue = mp.Queue(4096)
        # Start worker processes, leave 1 for TensorFlow
        workers = max(1, mp.cpu_count() - 1)
        if max_workers is not None:
            # In case mp.cpu_count() is not correct(e.g. when run inside Kubernetes image),
            # then you can use this
            workers = min(max_workers, workers)
        print("Using {} worker processes.".format(workers))
        for _ in range(workers):
            mp.Process(target=self.task,
                       args=(chunks, self.queue)).start()

    def task(self, chunks, queue):
        while True:
            random.shuffle(chunks)
            for chunk in chunks:
                with gzip.open(chunk, 'r') as chunk_file:
                    file_content = chunk_file.readlines()
                    item_count = len(file_content) // DATA_ITEM_LINES
                    for item_idx in range(item_count):
                        pick_offset = item_idx * DATA_ITEM_LINES
                        item = file_content[pick_offset:pick_offset + DATA_ITEM_LINES]
                        str_items = [str(line, 'ascii') for line in item]
                        success, data = convert_train_data(str_items)
                        if success:
                            queue.put(data)

    def parse_chunk(self):
        while True:
            yield self.queue.get()

def get_chunks(data_prefix):
    return glob.glob(data_prefix + "*.gz")

def main(args):
    train_data_prefix = args.pop(0)

    chunks = get_chunks(train_data_prefix)
    print("Found {0} chunks".format(len(chunks)))

    if not chunks:
        return

    output_path, KEY_OUTPUT_PATH = None, '--output-path'
    if KEY_OUTPUT_PATH in args:
        idx = args.index(KEY_OUTPUT_PATH)
        if len(args) - 1 == idx: # last element, wrong
            raise Exception(f'value expected for {KEY_OUTPUT_PATH}!')
        output_path = args.pop(idx+1)
        args.pop(idx)

    max_workers, KEY_MAX_WORKERS = None, '--max-workers'
    if KEY_MAX_WORKERS in args:
        idx = args.index(KEY_MAX_WORKERS)
        if len(args) - 1 == idx: # last element, wrong
            raise Exception(f'value expected for {KEY_MAX_WORKERS}!')
        max_workers = int(args.pop(idx+1))
        args.pop(idx)

    parser = ChunkParser(chunks, max_workers)

    dataset = tf.data.Dataset.from_generator(
        parser.parse_chunk, output_types=(tf.float32, tf.float32, tf.float32))
    dataset = dataset.shuffle(65536)
    dataset = dataset.batch(BATCH_SIZE)
    dataset = dataset.prefetch(16)
    iterator = dataset.make_one_shot_iterator()
    next_batch = iterator.get_next()

    tfprocess = TFProcess(next_batch, BOARD_SIZE, output_path)
    if args:
        restore_file = args.pop(0)
        tfprocess.restore(restore_file)
    while True:
        tfprocess.process(BATCH_SIZE)

if __name__ == "__main__":
    main(sys.argv[1:])
    mp.freeze_support()
