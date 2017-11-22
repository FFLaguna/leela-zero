#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>
#include <QtCore/QTextStream>
#include <QtCore/QStringList>
#include <QtCore/QPair>
#include <QtCore/QVector>
#include <QProcess>
#include <QThread>
#include <QFile>
#include <QCryptographicHash>
#include <QDir>
#include <QRegExp>
#include <QUuid>
#include <QDebug>
#include <iostream>
#include <functional>

const int eval_boardsize = 19;
const float eval_replace_rate = .55f;
const int eval_game_num = 100;

bool waitForReadyRead(QProcess& process) {
    while (!process.canReadLine() && process.state() == QProcess::Running) {
        process.waitForReadyRead(-1);
    }

    // somebody crashed
    if (process.state() != QProcess::Running) {
        return false;
    }

    return true;
}

bool sendGtpCommand(QTextStream &cerr, QProcess& proc, QString cmd) {
    QString cmdEndl(cmd);
    cmdEndl.append(qPrintable("\n"));

    proc.write(qPrintable(cmdEndl));
    proc.waitForBytesWritten(-1);
    if (!waitForReadyRead(proc)) {
        cerr << "Fail GTP cmd(" << cmd << "):" << "connect" << endl;
        return false;
    }
    char readbuff[256];
    auto read_cnt = proc.readLine(readbuff, 256);
    Q_ASSERT(read_cnt > 0);
    Q_ASSERT(readbuff[0] == '=');
    // Eat double newline from GTP protocol
    if (!waitForReadyRead(proc)) {
        cerr << "Fail GTP cmd(" << cmd << "):" << "read" << endl;
        return false;
    }
    read_cnt = proc.readLine(readbuff, 256);
    Q_ASSERT(read_cnt > 0);
    return true;
}

// if first process is winner, then return true;
// if second process is winner, then return false;
// according to go rules, first player/process is always play black;
bool eval_one_game(QTextStream &cerr, QProcess& first_process, QProcess& second_process){

    // ensure a clean board to start. 
    if (!sendGtpCommand(cerr, first_process,QStringLiteral("clear_board"))) {
        exit(EXIT_FAILURE);
    }
    if (!sendGtpCommand(cerr, second_process,QStringLiteral("clear_board"))) {
        exit(EXIT_FAILURE);
    }

    char readbuff[256];
    int read_cnt;

    QString winner;
    bool stop = false;
    bool black_to_move = true;
    bool black_resigned = false;
    bool first_to_move = true;
    int passes = 0;
    int move_num = 0;

    // Set infinite time
    if (!sendGtpCommand(cerr, first_process, QStringLiteral("time_settings 0 1 0"))) {
        exit(EXIT_FAILURE);
    }
    if (!sendGtpCommand(cerr, second_process, QStringLiteral("time_settings 0 1 0"))) {
        exit(EXIT_FAILURE);
    }
    //cerr << "Time successfully set." << endl;

    do {
        move_num++;
        QString move_cmd;
        if (black_to_move) {
            move_cmd = "genmove b\n";
        } else {
            move_cmd = "genmove w\n";
        }
        /// Send genmove to the right process
        auto proc = std::ref(first_process);
        if (!first_to_move) {
            proc = std::ref(second_process);
        }
        proc.get().write(qPrintable(move_cmd));
        proc.get().waitForBytesWritten(-1);
        if (!waitForReadyRead(proc)) {
            cerr << "Fail to waitForreadyRead for " << (first_to_move ? "first" : "second") << " process" << endl;
            exit(EXIT_FAILURE);
        }
        // Eat response
        read_cnt = proc.get().readLine(readbuff, 256);
        if (read_cnt <= 3 || readbuff[0] != '=') {
            cerr << "Error read " << read_cnt << " '" << readbuff << "'" << endl;
            second_process.terminate();
            first_process.terminate();
            exit(EXIT_FAILURE);
        }
        // Skip "= "
        QString resp_move(&readbuff[2]);
        resp_move = resp_move.simplified();

        // Eat double newline from GTP protocol
        if (!waitForReadyRead(proc)) {
            cerr << "Fail to waitForreadyRead for " << (first_to_move ? "first" : "second") << " process" << endl;
            exit(EXIT_FAILURE);
        }
        read_cnt = proc.get().readLine(readbuff, 256);
        Q_ASSERT(read_cnt > 0);

        //cerr << (black_to_move ? "B" : "W") << "(" << resp_move << ") ";
        //cerr.flush();

        QString move_side(QStringLiteral("play "));
        QString side_prefix;

        if (black_to_move) {
            side_prefix = QStringLiteral("b ");
        } else {
            side_prefix = QStringLiteral("w ");
        }

        move_side += side_prefix + resp_move + "\n";

        if (resp_move.compare(QStringLiteral("pass"),
                              Qt::CaseInsensitive) == 0) {
            passes++;
        } else if (resp_move.compare(QStringLiteral("resign"),
                                     Qt::CaseInsensitive) == 0) {
            passes++;
            stop = true;
            black_resigned = black_to_move;
        } else {
            passes = 0;
        }

        // Got move, swap sides now
        first_to_move = !first_to_move;
        black_to_move = !black_to_move;

        if (!stop) {
            auto next = std::ref(first_process);
            if (!first_to_move) {
                next = std::ref(second_process);
            }
            if (!sendGtpCommand(cerr, next, qPrintable(move_side))) {
                exit(EXIT_FAILURE);
            }
        }
    } while (!stop && passes < 2 && move_num < (eval_boardsize * eval_boardsize * 2));

    //cerr << endl;

    // Nobody resigned, we will have to count
    if (!stop) {
        // Ask for the winner
        first_process.write(qPrintable("final_score\n"));
        first_process.waitForBytesWritten(-1);
        if (!waitForReadyRead(first_process)) {
            cerr << "Fail to count for first process" << endl;
            exit(EXIT_FAILURE);
        }
        read_cnt = first_process.readLine(readbuff, 256);
        QString score(&readbuff[2]);
        // final_score returns
        // "= W+" or "= B+"
        if (readbuff[2] == 'W') {
            winner = QString(QStringLiteral("white"));
        } else if (readbuff[2] == 'B') {
            winner = QString(QStringLiteral("black"));
        }
        cerr << "Winner: " << winner << ".  Score: " << score;
        // Double newline
        if (!waitForReadyRead(first_process)) {
            cerr << "Fail to count for first process" << endl;
            exit(EXIT_FAILURE);
        }
        read_cnt = first_process.readLine(readbuff, 256);
        Q_ASSERT(read_cnt > 0);
    } else {
        if (black_resigned) {
            winner = QString(QStringLiteral("white"));
        } else {
            winner = QString(QStringLiteral("black"));
        }
    }

    if (winner.isNull()) {
        cerr << "No winner found" << endl;
        first_process.write(qPrintable("quit\n"));
        second_process.write(qPrintable("quit\n"));

        first_process.waitForFinished(-1);
        second_process.waitForFinished(-1);
        exit(EXIT_FAILURE);
    }

    // rest board 
    if (!sendGtpCommand(cerr, first_process,QStringLiteral("clear_board"))) {
        exit(EXIT_FAILURE);
    }
    if (!sendGtpCommand(cerr, second_process,QStringLiteral("clear_board"))) {
        exit(EXIT_FAILURE);
    }

    cerr.flush();

    if(winner.compare(QStringLiteral("black"), Qt::CaseInsensitive) == 0 ){
        return true;
    }
    Q_ASSERT(winner.compare(QStringLiteral("white"), Qt::CaseInsensitive) == 0);
    return false;
}

bool eval_one_model(QTextStream &cerr,
                    const QString &bestmodel_weightsname,
                    const QString &next_gen_weightsname)
{
    if (!QFileInfo::exists(bestmodel_weightsname)) {
        cerr << "Could not find best model weight : " << bestmodel_weightsname << endl;
        exit(EXIT_FAILURE);
    }

    if (!QFileInfo::exists(next_gen_weightsname)) {
        cerr << "Could not find next generation model weight : " << next_gen_weightsname << endl;
        exit(EXIT_FAILURE);
    }

    QString bestmodel_player_cmdline("./leelaz -g -n -m 30 -r 0 -w ");
    bestmodel_player_cmdline.append(bestmodel_weightsname);
    bestmodel_player_cmdline.append(" -p 800 --noponder");

    QString nextgen_model_player_cmdline("./leelaz -g -n -m 30 -r 0 -w ");
    nextgen_model_player_cmdline.append(next_gen_weightsname);
    nextgen_model_player_cmdline.append(" -p 800 --noponder");

    // cerr << "best model player command line :";
    // cerr << bestmodel_player_cmdline << endl;

    // cerr << "next generation model player command line :";
    // cerr << nextgen_model_player_cmdline << endl;

    QProcess first_process, second_process;
    first_process.start(bestmodel_player_cmdline);
    second_process.start(nextgen_model_player_cmdline);

    first_process.waitForStarted();
    second_process.waitForStarted();

    QVector<bool> results;
    // winning rate couts best model beats next gen model;
    float winning_rate = .0f;

    for(int i=0; i<eval_game_num; i++){
           bool best_model_win = eval_one_game(cerr, first_process, second_process);
           results.append(best_model_win);
           winning_rate = 1.0f * results.count(true) / results.size();
           cerr << (i+1) << " games played. Best winning rate: " << winning_rate << endl;
           if(results.count(true)>=eval_game_num * (1.0f - eval_replace_rate)){
               cerr << "lose count reach : " << results.count(true) << ", so give up challenge." << endl;
               break;
           }
           if(results.count(false)>=eval_game_num * eval_replace_rate){
               cerr << "win count reach : " << results.count(false) << ", so change best model." << endl;
               break;
           }
    }

    cerr << "best model beats next generation model at winning rate : " << winning_rate << endl;

    bool ret;
    if(winning_rate <= (1.0f - eval_replace_rate)){
        cerr << "next generation model becomes new best model !! congratulations !!" << endl;
        ret = true;
    }else{
        cerr << "best model is still old best model, next generation model can't beat it, try again :)" << endl;
        ret = false;
    }

    // Close down
    first_process.write(qPrintable("quit\n"));
    second_process.write(qPrintable("quit\n"));

    first_process.waitForFinished(-1);
    second_process.waitForFinished(-1);

    cerr.flush();

    return ret;
}

void copy_overwrite(QTextStream &cerr, const QString &src, const QString &dest) {
    if (QFile::exists(dest)) {
        QFile::remove(dest);
    }
    if (!QFile::copy(src, dest)){
        cerr << "Fail to copy from " << src << " to " << dest << endl;
        exit(EXIT_FAILURE);
    }
}

QString sha256(QTextStream &cerr, const QString &filename) {
    QFile file(filename);
    if (!file.open(QFile::ReadOnly))
    {
        cerr << "Sorry, Fail to open file " << filename<< endl;
        exit(EXIT_FAILURE);
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd())
    {
        hash.addData(file.read(8192));
    }
    QByteArray bytes = hash.result().toHex();
    QString ret(bytes);
    file.close();
    return ret;
}

QString first_line(QTextStream &cerr, const QString &filename) {
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        cerr << "fail opening " << filename << endl;
        exit(EXIT_FAILURE);
    }
    QTextStream in(&file);
    if (in.atEnd()) {
        file.close();
        cerr << "empty file " << filename << endl;
        exit(EXIT_FAILURE);
    }
    QString line = in.readLine();
    if (line.isEmpty()) {
        file.close();
        cerr << "empty first line in file " << filename << endl;
        exit(EXIT_FAILURE);
    } 
    file.close();
    return line;
}

void myexecute(QTextStream &cerr, const QString &cmd) {
    int result = QProcess::execute(cmd);
    if (result != 0) {
        cerr << "cmd(" << cmd << ") returns code " << result << endl;
        exit(EXIT_FAILURE);
    }
}

void prepend_line(QTextStream &cerr, const QString &fn, const QString &line) {
    QFile file(fn);
    file.open(QFile::ReadOnly | QFile::Text);
    QByteArray buffer = file.readAll();
    file.close();
    file.open(QFile::WriteOnly | QFile::Text);
    QTextStream out(&file);
    out << line << endl;
    out << buffer;
    file.close();
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTimer::singleShot(0, &app, SLOT(quit()));

    // Map streams
    QTextStream cin(stdin, QIODevice::ReadOnly);
    QTextStream cerr(stderr, QIODevice::WriteOnly);

    const QString fp_best_model_archive("/fds/exp/leela-zero/9x9/static/best_model_archive");
    const QString fp_best_model_hash("/fds/exp/leela-zero/9x9/static/best_model_hash");
    const QString fp_best_model_gz("/fds/exp/leela-zero/9x9/static/best_model.txt.gz");
    const QString fn_model_dir("/fds/exp/leela-zero/9x9/static/opt");

    while (true) {
        const QString expected_hash = first_line(cerr, fp_best_model_hash);

        const QString fp_best_model("best_model.txt");
        if (!QFile::exists(fp_best_model)) {
            QString fp_tmp_best_model_gz(fp_best_model+".gz");
            myexecute(cerr, "cp " + fp_best_model_gz + " " + fp_tmp_best_model_gz);
            myexecute(cerr, "gunzip -f " + fp_tmp_best_model_gz);
        }

        // check best model hash
        QString actual_hash = sha256(cerr, fp_best_model);
        if (expected_hash != actual_hash) {
            cerr << "wrong hash. Expected " << expected_hash << ", actual " << actual_hash << ". Delete local, sleep, and retry." << endl;
            myexecute(cerr, "rm " + fp_best_model);
            QThread::sleep(6);
            continue;
        }

        QDir dir(fn_model_dir);
        QStringList filters;
        filters << "leelaz-model-*.txt";
        dir.setNameFilters(filters);
        dir.setFilter(QDir::Files | QDir::NoSymLinks);
        QFileInfoList list = dir.entryInfoList();
        if (list.size() == 0) {
            cerr << "No model to evaluate. Sleeping..." << endl;
            QThread::sleep(60);
            continue;
        }
        for (int i = 0; i < list.size(); ++i) {
            QFileInfo fileInfo = list.at(i);
            QString fp_model = fileInfo.filePath();
            cerr << "Evaluating model:" << fp_model << endl;

            bool is_model_good = eval_one_model(cerr, fp_best_model, fp_model);

            if (is_model_good) {
                // compute hash
                QString hash = sha256(cerr, fp_model);
				QString fn_tmp_hash("best_model_hash.tmp");

                myexecute(cerr, "cp " + fp_best_model_hash+ " " + fn_tmp_hash);
                prepend_line(cerr, fn_tmp_hash, hash);

                myexecute(cerr, "gzip -f -k " + fp_model);
                QString model_gz(fp_model + ".gz");

                myexecute(cerr, "cp " + fn_tmp_hash + " " + fp_best_model_archive + "/best_model_hash_list");
                myexecute(cerr, "cp " + model_gz + " " + fp_best_model_archive + "/" + hash + ".gz");
                myexecute(cerr, "mv " + fn_tmp_hash + " " + fp_best_model_hash);
                myexecute(cerr, "cp " + model_gz + " " + fp_best_model_gz);
                myexecute(cerr, "rm " + model_gz);

                cerr << "best model updated. Hash: " << hash << endl;
            }

            myexecute(cerr, "rm " + fp_model);
            
            if (is_model_good) {
                myexecute(cerr, "rm " + fp_best_model);
                break;
            }
        }
    }

    return app.exec();
}
