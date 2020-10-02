#!/bin/python3

import argparse
import os
import requests as rq
import re

VERSION = 1.4


def is_up_to_date():
    """
    Check if the script is up to date
    :return: if the script is... up to date!
    """
    try:
        with rq.get(
                "https://raw.githubusercontent.com/IFT2245/NameValidator/master/validate.py") as version_check_req:
            raw = version_check_req.text
            version = re.search(r"VERSION = (\d+.\d+)", raw, flags=re.M)
            if version is None:
                print("Erreur en exécutant le script! Veuillez ouvrir un bug sur github!")
            else:
                online_version = float(version[1])
                return online_version == VERSION
    except rq.ConnectionError:
        print("Impossible de vérifier la version du script. En cas d'erreur, assurez-vous d'avoir la dernière version.")
        return True  # should still work


def extract_students(file: str):
    """
    Extract the student information from the file
    Will throw FileNotFoundError if the file is not found
    :param file: the file path
    :return:
    """
    with open(file, "r") as f:
        code = f.read()

        # Clean
        code = re.sub(r"\(.*?\)\s*?\{(\n|.)*?\n\}", "", code)
        code = re.sub(r"#include <[A-Za-z]+.h>\n", "", code)

        code = code.replace(":", " ") \
            .replace("-", "") \
            .replace("|", "") \
            .replace(">", "") \
            .replace("<", "") \
            .replace(".", "") \
            .replace("\\", "") \
            .replace("/", "")

        code = code.replace("Auteurs", "") \
            .replace("Auteur", "")

        code = re.sub(r"\*+", "*", code)

        name_and_id_regex = r"([A-Za-zÀ-ÿ]+) ([A-Za-zÀ-ÿ]+\ ){1,}([0-9]{6,8})"

        # Trim spaces
        names = []
        for match in re.finditer(name_and_id_regex, code):
            lo, hi = match.span()
            extracted_match = match.string[lo: hi]
            names.append(tuple((part.lstrip().rstrip() for part in extracted_match.split(" "))))

        return names


if __name__ == '__main__':

    if not is_up_to_date():
        print("Le script n'est pas à jour. Veuillez le mettre à jour avec la dernière version en ligne.")
        exit(1)

    parser = argparse.ArgumentParser(description='Valide les noms d''étudiants dans un fichier c.')
    parser.add_argument('path', metavar='p', type=str, nargs=1,
                        help='Le chemin du fichier')

    args = parser.parse_args()

    dirname = os.path.dirname(__file__)
    filename = os.path.join(dirname, args.path[0])

    try:
        results = extract_students(filename)
        print("---------------- Résultat ------------------")
        if len(results) > 0:
            for name in results:
                print(f"Étudiant trouvé: '{' '.join(name[:-1])}' avec le matricule {name[-1]}.")
            exit(0)
        else:
            print("Aucun étudiant trouvé. Veuillez changer votre entête de fichier.")
            exit(1)
    except FileNotFoundError as e:
        print(f"Le fichier {e} n'a pas été trouvé. Erreur.")
        exit(1)
