/*
    This file is part of Leela Zero.
    Copyright (C) 2017 Gian-Carlo Pascutto

    Leela Zero is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Leela Zero is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Leela Zero.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>
#include <QtCore/QTextStream>
#include <QtCore/QStringList>
#include <QCommandLineParser>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QCryptographicHash>
#include <iostream>
#include "Game.h"

constexpr int AUTOGTP_VERSION = 3;

static const QString mydir("/fds/exp/leela-zero/9x9/static/");

bool fetch_best_network_hash(QTextStream& cerr, QString& nethash) {
    QFile inputFile(mydir + "best_model_hash");
    if (!inputFile.open(QIODevice::ReadOnly)) {
        cerr << "fail opening best_model_hash" << endl;
        exit(EXIT_FAILURE);
    }
    QTextStream in(&inputFile);
    if (in.atEnd()) {
        inputFile.close();
        cerr << "empty best_model_hash" << endl;
        exit(EXIT_FAILURE);
    }
    nethash = in.readLine();
    if (nethash.isEmpty()) {
        inputFile.close();
        cerr << "empty hash" << endl;
        exit(EXIT_FAILURE);
    } 
    inputFile.close();
    cerr << "best model hash: " << nethash << endl;
    return true;
}

bool fetch_best_network(QTextStream& cerr, QString& netname) {
    if (QFileInfo::exists(netname)) {
        cerr << "Already downloaded network." << endl;
        return true;
    }

    QString outfile(netname+".gz");
    bool ret = QFile::copy(mydir + "best_model.txt.gz", "./"+outfile);
    if (!ret) {
        cerr << "Fail to copy best model" << endl;
        return false;
    }

#ifdef WIN32
    QProcess::execute("gzip.exe -d -k -q " + outfile);
#else
    QProcess::execute("gunzip -k -q " + outfile);
#endif
    // Remove extension (.gz)
    outfile.chop(3);

    // check file sha256 equals to netname
    // QFile tmpFileHash(outfile);
    // if (!tmpFileHash.open(QFile::ReadOnly)) {
    //     cerr << "Sorry, Fail to open file " + outfile << endl;
    //     exit(EXIT_FAILURE);
    // }
    // QCryptographicHash hash(QCryptographicHash::Sha256);
    // if (!hash.addData(&tmpFileHash)) {
    //     cerr << "Sorry, Fail to compute sha256." << endl;
    //     exit(EXIT_FAILURE);
    // }
    // QByteArray sha256_output = hash.result();
    // QString sha256_str(sha256_output);
    // if (sha256_str.compare(netname) != 0) {
    //     tmpFileHash.close();
    //     cerr << "SHA256 of " + outfile + " is " + sha256_str + ", but expected to be " + netname << endl;
    //     exit(EXIT_FAILURE);
    // }
    // tmpFileHash.close();

    cerr << "Net filename: " << outfile << endl;
    netname = outfile;

    // DEBUG purpose

    QFileInfoList list = QDir().entryInfoList();
    for (int i = 0; i < list.size(); ++i) {
        QFileInfo fileInfo = list.at(i);

        cerr << " filename: " <<  fileInfo.fileName() << endl;
    }

    return true;
}

bool upload_data(QTextStream& cerr, const QString& netname, QString sgf_output_path) {
    // Find output SGF and txt files
    QDir dir;
    QStringList filters;
    filters << "*.sgf";
    dir.setNameFilters(filters);
    dir.setFilter(QDir::Files | QDir::NoSymLinks);

    QFileInfoList list = dir.entryInfoList();
    for (int i = 0; i < list.size(); ++i) {
        QFileInfo fileInfo = list.at(i);
        QString sgf_file = fileInfo.fileName();
        QString data_file = sgf_file;
        // Save first if requested
        if (!sgf_output_path.isEmpty()) {
            QFile(sgf_file).copy(sgf_output_path + '/' + fileInfo.fileName());
        }
        // Cut .sgf, add .txt.0.gz
        data_file.chop(4);
        data_file += ".txt.0.gz";
        // Gzip up the sgf too
#ifdef WIN32
        QProcess::execute("gzip.exe " + sgf_file);
#else
        QProcess::execute("gzip " + sgf_file);
#endif
        sgf_file += ".gz";

        QString target_sgf_folder(mydir + netname + "/sgf/");
        if (!QDir(target_sgf_folder).exists()) {
            if (!QDir().mkpath(target_sgf_folder)) {
                cerr << "Fail to mdir " << target_sgf_folder << endl;
                return false;
            }
        }
        QString target_train_data_folder(mydir + netname + "/train_data/");
        if (!QDir(target_train_data_folder).exists()) {
            if (!QDir().mkpath(target_train_data_folder)) {
                cerr << "Fail to mdir " << target_train_data_folder << endl;
                return false;
            }
        }
        if (!QFile(sgf_file).copy(target_sgf_folder + sgf_file)) {
            cerr << "Fail to copy " << sgf_file << endl;
            return false;
        }
        if (!QFile(data_file).copy(target_train_data_folder + data_file)) {
            cerr << "Fail to copy " << data_file << endl;
            return false;
        }
        dir.remove(sgf_file);
        dir.remove(data_file);
    }
    return true;
}

bool run_one_game(QTextStream& cerr, const QString& weightsname) {

    Game game(weightsname, cerr);
    if(!game.gameStart()) {
        return false;
    }
    do {
        game.move();
        if(!game.waitForMove()) {
            return false;
        }
        game.readMove();
    } while (game.nextMove());
    cerr << "Game has ended." << endl;
    if (game.getScore()) {
        game.writeSgf();
        game.dumpTraining();
    }
    cerr << "Stopping engine." << endl;
    game.gameQuit();
    return true;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("autogtp");
    app.setApplicationVersion(QString("v%1").arg(AUTOGTP_VERSION));
    QTimer::singleShot(0, &app, SLOT(quit()));

    QCommandLineOption keep_sgf_option(
        { "k", "keep-sgf" }, "Save SGF files after each self-play game.",
                             "output directory");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(keep_sgf_option);
    parser.process(app);

    // Map streams
    QTextStream cin(stdin, QIODevice::ReadOnly);
    QTextStream cout(stdout, QIODevice::WriteOnly);
#if defined(LOG_ERRORS_TO_FILE)
    // Log stderr to file
    QFile caFile("output.txt");
    caFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append);
    if(!caFile.isOpen()){
        qDebug() << "- Error, unable to open" << "outputFilename" << "for output";
    }
    QTextStream cerr(&caFile);
#else
    QTextStream cerr(stderr, QIODevice::WriteOnly);
#endif

    cerr << "autogtp v" << AUTOGTP_VERSION << endl;

    if (parser.isSet(keep_sgf_option)) {
        if (!QDir().mkpath(parser.value(keep_sgf_option))) {
            cerr << "Couldn't create output directory for self-play SGF files!"
                 << endl;
            return EXIT_FAILURE;
        }
    }

    auto success = true;
    auto games_played = 0;

    do {
        QString netname;
        success &= fetch_best_network_hash(cerr, netname);
        success &= fetch_best_network(cerr, netname);
        success &= run_one_game(cerr, netname);
        success &= upload_data(cerr, netname, parser.value(keep_sgf_option));
        games_played++;
        cerr << games_played << " games played." << endl;
    } while (success);

    cerr.flush();
    cout.flush();
    return app.exec();
}
