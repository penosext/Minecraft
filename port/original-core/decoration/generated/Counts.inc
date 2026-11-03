Counts counts(int id) { Counts c;
c.treeCount = 0;
c.flowerCount = 2;
c.grassCount = 1;
c.reedsCount = 0;
c.cactusCount = 0;
switch(id) {
case 1:
c.treeCount = -999;
c.flowerCount = 4;
c.grassCount = 10;
break;
case 2: case 17:
c.treeCount = -999;
c.reedsCount = 50;
c.cactusCount = 10;
break;
case 4: case 18:
c.treeCount = 10;
c.grassCount = 2;
break;
case 5: case 19:
c.treeCount = 10;
c.grassCount = 1;
break;
case 6:
c.treeCount = 2;
c.flowerCount = -999;
c.reedsCount = 10;
break;
case 14: case 15:
c.treeCount = -100;
c.flowerCount = -100;
c.grassCount = -100;
break;
case 16:
c.treeCount = -999;
c.reedsCount = 0;
c.cactusCount = 0;
break;
case 21: case 22:
c.treeCount = 50;
c.grassCount = 25;
c.flowerCount = 4;
break;
} return c; }
