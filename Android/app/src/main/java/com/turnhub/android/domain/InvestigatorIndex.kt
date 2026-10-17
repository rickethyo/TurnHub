package com.turnhub.android.domain

import kotlin.random.Random

/** Factual common-skill associations and reference links, checked 2026-10-17.
 * Source: Make Craft Game's Death May Die index. No source code, card art or rules copied.
 * Spellings are mapped to TurnHub's existing investigator identities.
 */
object InvestigatorIndex {
    const val SOURCE = "https://makecraftgame.com/death-may-die/"
    private val common = mapOf(
        "Lord Adam Benchley" to listOf("Marksman", "Toughness"),
        "Ahmed Yasin" to listOf("Stealth", "Arcane Mastery"),
        "Borden" to listOf("Stealth", "Swiftness"),
        "Elizabeth Ives" to listOf("Marksman", "Stealth"),
        "Fatima Safar" to listOf("Swiftness", "Arcane Mastery"),
        "Sergeant Ian Welles" to listOf("Swiftness", "Brawling"),
        "John Morgan" to listOf("Swiftness", "Toughness"),
        "Rasputin" to listOf("Arcane Mastery", "Brawling"),
        "Sister Beth" to listOf("Brawling", "Toughness"),
        "The Kid" to listOf("Marksman", "Arcane Mastery"),
        "Adilah Vidal" to listOf("Arcane Mastery", "Brawling"),
        "Alex Parker" to listOf("Stealth", "Swiftness"),
        "Ariele Venturi" to listOf("Marksman", "Stealth"),
        "Hailia Hovath" to listOf("Stealth", "Arcane Mastery"),
        "Father Luke" to listOf("Arcane Mastery", "Toughness"),
        "Margarethe Richter" to listOf("Stealth", "Brawling"),
        "Sergeant Mario Braghieri" to listOf("Marksman", "Toughness"),
        "Mary Diaz" to listOf("Marksman", "Swiftness"),
        "Sam Wood" to listOf("Swiftness", "Toughness"),
        "Machinist's Mate Tony Boyle" to listOf("Brawling", "Toughness"),
        "Scarlett Hayes" to listOf("Swiftness", "Brawling"),
        "Eddie the Head" to listOf("Swiftness", "Brawling"),
        "Al Capone" to listOf("Marksman", "Swiftness"),
        "Albert Einstein" to listOf("Arcane Mastery", "Toughness"),
        "Ernest Hemingway" to listOf("Marksman", "Arcane Mastery"),
        "Fleur Dubois" to listOf("Marksman", "Stealth"),
        "Marcelo 'Gonk' Gonçalves" to listOf("Brawling", "Toughness"),
        "Josephine Leclerc" to listOf("Marksman", "Swiftness"),
        "Julia Hollister" to listOf("Swiftness", "Brawling"),
        "Maki Smith" to listOf("Marksman", "Stealth"),
        "Marjorie 'Margie' Westmoreland" to listOf("Swiftness", "Arcane Mastery"),
        "Maxim Grey" to listOf("Arcane Mastery", "Brawling"),
        "Meryl Starling" to listOf("Stealth", "Arcane Mastery"),
        "Olivia Duchamp" to listOf("Stealth", "Swiftness"),
        "'Pops' Duperon" to listOf("Arcane Mastery", "Toughness"),
        "Richard Irons" to listOf("Swiftness", "Toughness"),
        "Roxie Mitchell Williams" to listOf("Brawling", "Toughness"),
        "Tina Macshane" to listOf("Stealth", "Brawling"),
        "Professor Vincent Haas" to listOf("Swiftness", "Arcane Mastery"),
        "Walter Bagget" to listOf("Marksman", "Arcane Mastery"),
        "Allan Jenkins" to listOf("Marksman", "Toughness"),
        "Annabelle Laveau" to listOf("Arcane Mastery", "Toughness"),
        "Jack Muñoz" to listOf("Swiftness", "Brawling"),
        "Lysa" to listOf("Stealth", "Arcane Mastery"),
        "Rocco" to listOf("Marksman", "Brawling"),
        "Tamara" to listOf("Stealth", "Swiftness"),
        "Ezra" to listOf("Stealth", "Brawling"),
        "Olmstead" to listOf("Marksman", "Swiftness"),
        "Ethan" to listOf("Swiftness", "Arcane Mastery"),
        "Veronique" to listOf("Marksman", "Arcane Mastery"),
        "Virginia Lyman" to listOf("Stealth", "Toughness"),
        "Agatha May" to listOf("Marksman", "Stealth"),
        "Huikong" to listOf("Swiftness", "Toughness"),
        "Julien Dulik" to listOf("Stealth", "Arcane Mastery"),
        "Leon Barnes" to listOf("Marksman", "Swiftness"),
        "Mike Kozlov" to listOf("Brawling", "Toughness"),
        "Jakub Peters" to listOf("Swiftness", "Brawling"),
        "Ruth Weber" to listOf("Stealth", "Swiftness"),
        "Sally Withers" to listOf("Arcane Mastery", "Toughness"),
        "Sandra Atkinson" to listOf("Arcane Mastery", "Brawling"),
        "Stella Fontaine" to listOf("Marksman", "Arcane Mastery"),
        "Aniko Biko" to listOf("Swiftness", "Arcane Mastery"),
        "Bruno Vitale" to listOf("Stealth", "Brawling"),
        "Gary West" to listOf("Swiftness", "Toughness"),
        "John Fernsby" to listOf("Marksman", "Swiftness"),
        "Rosario Guerra" to listOf("Marksman", "Toughness"),
        "Sylvia Doyle" to listOf("Arcane Mastery", "Brawling"),
        "Agnes Fortin" to listOf("Stealth", "Toughness"),
        "Amelie Crowe" to listOf("Marksman", "Arcane Mastery"),
        "Amir Sesay" to listOf("Swiftness", "Arcane Mastery"),
        "Antonio Senatore" to listOf("Arcane Mastery", "Brawling"),
        "Archie Fort" to listOf("Brawling", "Toughness"),
        "Buck Hall" to listOf("Marksman", "Toughness"),
        "Charles Hollar" to listOf("Arcane Mastery", "Brawling"),
        "Craig Adamson" to listOf("Marksman", "Stealth"),
        "Salvador Dalí" to listOf("Marksman", "Arcane Mastery"),
        "Don Hayworth" to listOf("Arcane Mastery", "Toughness"),
        "Dorothy Parker" to listOf("Stealth", "Swiftness"),
        "Eve Blanchet" to listOf("Stealth", "Brawling"),
        "George Rodriguez" to listOf("Arcane Mastery", "Toughness"),
        "Harry Houdini" to listOf("Stealth", "Arcane Mastery"),
        "Isadora Rocha" to listOf("Swiftness", "Brawling"),
        "James Dalton" to listOf("Marksman", "Toughness"),
        "Jim Cannon" to listOf("Marksman", "Swiftness"),
        "Karen Mendel" to listOf("Brawling", "Toughness"),
        "Martin Horner" to listOf("Swiftness", "Toughness"),
        "Michelle Hart" to listOf("Stealth", "Brawling"),
        "Robert Gray" to listOf("Marksman", "Arcane Mastery"),
        "Ryan Martell" to listOf("Marksman", "Stealth"),
        "Ikeda Saito" to listOf("Swiftness", "Brawling"),
        "Steve Wainwright" to listOf("Swiftness", "Toughness"),
        "Taylor Renner" to listOf("Stealth", "Swiftness"),
        "Tom Gilman" to listOf("Marksman", "Swiftness"),
        "Vanessa Ortiz" to listOf("Swiftness", "Arcane Mastery"),
    )
    fun skills(name: String?): List<String>? = common[name]
    val filters = listOf("Arcane Mastery", "Brawling", "Marksman", "Stealth", "Swiftness", "Toughness")

    fun matches(name: String, query: String, skill: String?): Boolean {
        val names = InvestigatorSkills.forInvestigator(name).map { it.name }
        return (skill == null || names.any { it.equals(skill, ignoreCase = true) }) &&
            (name.contains(query.trim(), ignoreCase = true) || names.any { it.contains(query.trim(), ignoreCase = true) })
    }

    fun random(query: String, skill: String?, taken: Set<String>, random: Random = Random.Default): String? =
        DeathMayDie.all.filter { name -> matches(name, query, skill) && taken.none { it.equals(name, ignoreCase = true) } }
            .randomOrNull(random)
}
