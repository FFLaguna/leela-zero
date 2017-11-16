import argparse
import random
import tempfile
import os

def my_rand():
    return random.gauss(0, 1)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--board-size", help="board size", type=int, default=19)
    parser.add_argument("--output", help="output path", default=None)
    args = parser.parse_args()
    bsz = args.board_size
    fn = args.output
    if fn is None:
        dir = tempfile.mkdtemp()
        fn = os.path.join(dir, f'/tmp/random_weights_{bsz}.txt')

    n_weight_per_lines = [
        20736, 128, 128, 128,  # CNN BLOCK
        147456, 128, 128, 128, 147456, 128, 128, 128, # residual 1
        147456, 128, 128, 128, 147456, 128, 128, 128, # residual 2
        147456, 128, 128, 128, 147456, 128, 128, 128, # residual 3
        147456, 128, 128, 128, 147456, 128, 128, 128, # residual 4
        147456, 128, 128, 128, 147456, 128, 128, 128, # residual 5
        147456, 128, 128, 128, 147456, 128, 128, 128, # residual 6
        256, 2, 2, 2, bsz*bsz*2*(bsz*bsz+1), bsz*bsz+1, # policy head
        128, 1, 1, 1, bsz*bsz*256, 256, 256, 1 # value head
    ]

    with open(fn, 'wt') as f:
        f.write('1\n')  # weight version
        for n in n_weight_per_lines:
            l = ''
            for r in (my_rand() for _ in range(n)):
                l += f'{r:.8f} '
            l = l[:-1]  # cut the final blank
            l += '\n'  # make new line
            f.write(l)

    print(f'{fn} generated!')


if __name__ == '__main__':
    main()
