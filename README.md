# **PathFinders**

URL Scanner est un outil de scan d'URLs écrit en **C**, utilisant la bibliothèque **libcurl**. Il permet d'explorer des chemins cachés sur un serveur web en utilisant une wordlist et d'analyser les réponses HTTP pour détecter les ressources accessibles.

---

## **Fonctionnalités principales**

### 🌐 **Exploration d'URLs**
- Scanne des URLs en se basant sur une **wordlist** spécifiée.
- Supporte les niveaux d'exploration pour naviguer dans les sous-répertoires.


### ⚙️ **Options avancées**
- **Exploration multi-niveaux** : Permet de définir la profondeur de scan.
- **Rapport détaillé** : Enregistre les résultats dans un fichier texte.

---

## **Installation**

### Prérequis
- **GCC** (Compilateur C)
- **libcurl** (bibliothèque de gestion des requêtes HTTP)

### Installation
Clonez le dépôt Github :
```bash
git clone https://github.com/Chocofresh14/PathFinders.git
```

Allez dans le dossier `PathFinders` :
```bash
cd PathFinders
```

Exécutez le fichier shell pour l'installer et le compiler :
```bash
sudo bash install.sh
```

---

## **Options d'utilisation**

| Option              | Description                                                        |
|----------------------|--------------------------------------------------------------------|
| `<url>`             | Spécifie l'URL de base à scanner.                                   |
| `<wordlist>`        | Spécifie le chemin du fichier de wordlist.                         |
| `-n <niveau>`       | Définit le niveau de profondeur du scan.                           |
| `-o <fichier>`      | Enregistre les résultats dans un fichier texte.                    |

### Exemple
```bash
pfinders http://example.com wordlist.txt -n 2 -o resultats.txt
```

---

## **Avertissements**
- Cet outil doit être utilisé uniquement à des fins légales et éthiques.
- Assurez-vous d'avoir l'autorisation explicite avant de scanner un site web.

---

## **Contribution**
Les contributions sont les bienvenues ! Si vous avez des idées ou des suggestions, n'hésitez pas à ouvrir une **issue** ou à soumettre une **pull request**.

---

## **Licence**
Ce projet est sous licence **GPLv3**. Vous êtes libre de l'utiliser, le modifier et le distribuer tout en respectant les termes de la licence.

