import web
import os
from datetime import datetime
import random
import string

urls = (
    '/best-network-hash', 'Hash',
    '/best-network', 'BestNetwork',
    '/submit', 'Submit',
)

class Hash:
    def GET(self):
        with open('./static/hash', 'rt') as f:
            l = f.readline()
            l += '2' # version
            print(l)
            return l

class BestNetwork:
    def GET(self):
        return open('./static/random_weights_9.txt.gz', "rb").read()

class Submit:
    def POST(self):
        data = web.input()
        model_hash = data.networkhash
        v = data.clientversion
        sgf = data.sgf
        training_data = data.trainingdata

        timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        random_postfix = ''.join(random.choices(string.ascii_uppercase + string.digits, k=6))

        dir = f'./static/train_data/{model_hash}'
        os.makedirs(dir, exist_ok=True)

        fn_sgf = os.path.join(dir, f'v{v}-{timestamp}-{random_postfix}.sgf.gz')
        with open(fn_sgf, 'wb') as f:
            f.write(sgf)
            print(f'{fn_sgf} saved.')
        fn_training_data = os.path.join(dir, f'v{v}-{timestamp}-{random_postfix}.txt.0.gz')

        with open(fn_training_data, 'wb') as f:
            f.write(training_data)
            print(f'{fn_training_data} saved')

if __name__ == "__main__":
    app = web.application(urls, globals())
    app.run()
