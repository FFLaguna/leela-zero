#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>
#include <QtCore/QTextStream>
#include <QtCore/QStringList>
#include <QtCore/QPair>
#include <QtCore/QVector>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QRegExp>
#include <QUuid>
#include <QDebug>
#include <iostream>
#include <functional>

const int eval_boardsize = 9;
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

bool sendGtpCommand(QProcess& proc, QString cmd) {
    QString cmdEndl(cmd);
    cmdEndl.append(qPrintable("\n"));

    proc.write(qPrintable(cmdEndl));
    proc.waitForBytesWritten(-1);
    if (!waitForReadyRead(proc)) {
        return false;
    }
    char readbuff[256];
    auto read_cnt = proc.readLine(readbuff, 256);
    Q_ASSERT(read_cnt > 0);
    Q_ASSERT(readbuff[0] == '=');
    // Eat double newline from GTP protocol
    if (!waitForReadyRead(proc)) {
        return false;
    }
    read_cnt = proc.readLine(readbuff, 256);
    Q_ASSERT(read_cnt > 0);
    return true;
}

// if first process is winner, then return true;
// if second process is winner, then return false;
// according to go rules, first player/process is always play black;
bool eval_one_game(QProcess& first_process, QProcess& second_process, QTextStream &out_stream){

    // ensure a clean board to start. 
    if (!sendGtpCommand(first_process,QStringLiteral("clear_board"))) {
        exit(EXIT_FAILURE);
    }
    if (!sendGtpCommand(second_process,QStringLiteral("clear_board"))) {
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
    if (!sendGtpCommand(first_process,
                        QStringLiteral("time_settings 0 1 0"))) {
        exit(EXIT_FAILURE);
    }
    if (!sendGtpCommand(second_process,
                        QStringLiteral("time_settings 0 1 0"))) {
        exit(EXIT_FAILURE);
    }
    out_stream << "Time successfully set." << endl;

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
            exit(EXIT_FAILURE);
        }
        // Eat response
        read_cnt = proc.get().readLine(readbuff, 256);
        if (read_cnt <= 3 || readbuff[0] != '=') {
            out_stream << "Error read " << read_cnt
                 << " '" << readbuff << "'" << endl;
            second_process.terminate();
            first_process.terminate();
            exit(EXIT_FAILURE);
        }
        // Skip "= "
        QString resp_move(&readbuff[2]);
        resp_move = resp_move.simplified();

        // Eat double newline from GTP protocol
        if (!waitForReadyRead(proc)) {
            exit(EXIT_FAILURE);
        }
        read_cnt = proc.get().readLine(readbuff, 256);
        Q_ASSERT(read_cnt > 0);

        out_stream << "Move received: " << resp_move << endl;

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
            if (!sendGtpCommand(next, qPrintable(move_side))) {
                exit(EXIT_FAILURE);
            }
        }
    } while (!stop && passes < 2 && move_num < (eval_boardsize * eval_boardsize * 2));

    // Nobody resigned, we will have to count
    if (!stop) {
        // Ask for the winner
        first_process.write(qPrintable("final_score\n"));
        first_process.waitForBytesWritten(-1);
        if (!waitForReadyRead(first_process)) {
            exit(EXIT_FAILURE);
        }
        read_cnt = first_process.readLine(readbuff, 256);
        QString score(&readbuff[2]);
        out_stream << "Score: " << score;
        // final_score returns
        // "= W+" or "= B+"
        if (readbuff[2] == 'W') {
            winner = QString(QStringLiteral("white"));
        } else if (readbuff[2] == 'B') {
            winner = QString(QStringLiteral("black"));
        }
        out_stream << "Winner: " << winner << endl;
        // Double newline
        if (!waitForReadyRead(first_process)) {
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
        out_stream << "No winner found" << endl;
        first_process.write(qPrintable("quit\n"));
        second_process.write(qPrintable("quit\n"));

        first_process.waitForFinished(-1);
        second_process.waitForFinished(-1);
        exit(EXIT_FAILURE);
    }

    // rest board 
    if (!sendGtpCommand(first_process,QStringLiteral("clear_board"))) {
        exit(EXIT_FAILURE);
    }
    if (!sendGtpCommand(second_process,QStringLiteral("clear_board"))) {
        exit(EXIT_FAILURE);
    }

    out_stream.flush();

    if(winner.compare(QStringLiteral("black"), Qt::CaseInsensitive) == 0 ){
        return true;
    }
    Q_ASSERT(winner.compare(QStringLiteral("white"), Qt::CaseInsensitive) == 0);
    return false;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTimer::singleShot(0, &app, SLOT(quit()));

    // Map streams
    QTextStream cin(stdin, QIODevice::ReadOnly);
    QTextStream cout(stdout, QIODevice::WriteOnly);
    QTextStream cerr(stderr, QIODevice::WriteOnly);


    cerr << "evaluation two models v0.1" << endl;

    QStringList slargs = app.arguments();

    if (slargs.size() != 3) {
        cerr << "Invalid number of arguments (" << slargs.size() << ")" << endl;
        exit(EXIT_FAILURE);
    }

    QString bestmodel_weightsname = slargs[1];
    QString next_gen_weightsname = slargs[2];
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

    cerr << "best model player command line :" << endl;
    cerr << bestmodel_player_cmdline << endl;

    cerr << "next generation model player command line :" << endl;
    cerr << nextgen_model_player_cmdline << endl;

    QProcess first_process, second_process;
    first_process.start(bestmodel_player_cmdline);
    second_process.start(nextgen_model_player_cmdline);

    first_process.waitForStarted();
    second_process.waitForStarted();

    QVector<bool> results;
    // winning rate couts best model beats next gen model;
    float winning_rate = .0f;

    for(int i=0; i<eval_game_num; i++){
           bool best_model_win = eval_one_game(first_process, second_process, cerr);
           results.append(best_model_win);
           winning_rate = results.count(true)/results.size();
           if(results.count(true)>=eval_game_num * (1.0f - eval_replace_rate)){
               cerr << "lose count reach : " << results.count(true) << ", so give up challenge." << endl;
               break;
           }
           if(results.count(false)>=eval_game_num * eval_replace_rate){
               cerr << "win count reach : " << results.count(false) << ", so change best model." << endl;
           }
    }

    winning_rate = results.count(true)/results.size();
    cerr << "best model beats next generation model at winning rate : " << winning_rate << endl;

    if(winning_rate <= (1.0f - eval_replace_rate)){
        cerr << "next generation model becomes new best model !! congratulations !!" << endl;
    }else{
        cerr << "best model is still old best model, next generation model can't beat it, try again :)" << endl;
    }

    // Close down
    first_process.write(qPrintable("quit\n"));
    second_process.write(qPrintable("quit\n"));

    first_process.waitForFinished(-1);
    second_process.waitForFinished(-1);

    cerr.flush();
    cout.flush();
    return app.exec();
}
