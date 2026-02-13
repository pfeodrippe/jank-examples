# La Voiture
# Une fiction interactive

---
title: La Voiture
author: Anonymous
theme: noir
speakers:
  V: Voiture
  M: Manguier
colors:
  V: [180, 160, 140]
  M: [100, 140, 120]
  accent: [180, 80, 70]
---

#∆V [id:intro_car_waiting] Une voiture avec ses feux rouges attend quelqu’un, il a air d’être là il y a plusieurs heures. Le brouillard est épais, vous ne voyez rien d’autre que cette machine.
Vous sentez un souffle chaud derrière vous.
:: Se retourner.
	#∆M [id:reply_only_manguiers] Il n’existent que des manguiers aux alentours.
:: Inspecter la voiture.
	#∆V [id:inspect_driver_door_smell] À la porte du conducteur, l’odeur de cigarette à l’orange envahit le silence. Le terrain de mangrove recouvre vos semelles, c’est inconfortable, même si vous y êtes déjà venu de nombreuses fois.
	:: Ouvrir la poignée.
		#∆V [id:handle_clicks_open] La poignée est froide et humide et en quelque sorte, vous entendez un clic, elle s’ouvre.
		Dans l’intérieur, il existe un dossier épais sur le siège passager avec un petit autocollant à l’avant.
		:: Lire le l’autocollant.
		    [id:sticker_maman] « Maman… »
	:: Frapper à la porte.
		#∆V [id:visage_decu] Il n'y a que ton visage deçu.
